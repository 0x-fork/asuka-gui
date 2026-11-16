//go:build windows

// asuka/purchase on Windows: the Microsoft Store (Windows.Services.Store),
// for an app installed from it; one that isn't (no package identity:
// asuka dev, a build installed by hand) has no store. Products are the
// app's add-ons, by the product ID Partner Center gave each (its
// InAppOfferToken) or their Store ID. A consumable's purchase is a
// transaction until the app finishes it (the Store's fulfillment: the
// user can't buy it again until then, and those left unfulfilled come
// again at the next start); a durable's or a subscription's is its
// license. The Store says when the user's licenses change (bought on
// another PC, renewed, refunded): new ones come through events(), a
// durable's gone as revoked. The Store's calls wait, so each runs on its
// call's goroutine in COM's multithreaded apartment; the events'
// callbacks are posted to the main thread.
package purchase

import (
	"context"
	"errors"
	"strings"
	"sync"
	"time"

	"github.com/egoist/asuka/internal/windows"
	"github.com/egoist/asuka/native"
)

type msStore struct {
	mu      sync.Mutex
	ctx     *windows.StoreContext
	err     error
	once    sync.Once
	addOns  map[string]windows.StoreAddOn        // by Store ID
	active  map[string]windows.StoreAddOnLicense // the licenses last seen active, by the add-on's Store ID
	bought  map[string]bool                      // Store IDs bought here, whose license isn't news
	kinds   map[string]Kind                      // what the app declared
	kept    []Event
	readers map[int]func(Event)
	next    int
}

func newStore() (Store, error) {
	if !windows.Packaged() {
		return nil, ErrNotAvailable
	}
	s := &msStore{addOns: map[string]windows.StoreAddOn{}, active: map[string]windows.StoreAddOnLicense{}, bought: map[string]bool{},
		kinds: map[string]Kind{}, readers: map[int]func(Event){}}
	// Listening from the start: what the user bought while the app
	// didn't read comes to the first events().
	go windows.WithCOM(func() { s.context() })
	return s, nil
}

// storeIDOf is an add-on's Store ID from a SKU's (9NBLGGH4R315/0010).
func storeIDOf(sku string) string {
	id, _, _ := strings.Cut(sku, "/")
	return id
}

// com runs f in COM's multithreaded apartment, with the Store's context.
func (s *msStore) com(f func(ctx *windows.StoreContext) error) error {
	var err error
	windows.WithCOM(func() {
		var ctx *windows.StoreContext
		if ctx, err = s.context(); err == nil {
			err = f(ctx)
		}
	})
	return err
}

// context is the Store's context, made once (over the app's window, for
// its dialogs), with its add-ons and licenses read and their changes
// followed.
func (s *msStore) context() (*windows.StoreContext, error) {
	s.once.Do(func() {
		var hwnd uintptr
		native.OnMain(func() { hwnd = windows.ForegroundWindow() })
		s.ctx, s.err = windows.NewStoreContext(hwnd)
		if s.err != nil {
			return
		}
		s.refresh(s.ctx)
		// The licenses as the app starts: a consumable's not yet
		// fulfilled is a transaction left unfinished.
		ls, _ := s.ctx.Licenses()
		s.mu.Lock()
		for _, l := range ls {
			if !l.Active {
				continue
			}
			id := storeIDOf(l.SkuStoreID)
			s.active[id] = l
			if a, ok := s.addOns[id]; ok && strings.HasSuffix(a.Kind, "Consumable") {
				s.give(s.transaction(a, ""))
			}
		}
		s.mu.Unlock()
		ctx := s.ctx
		ctx.OnLicensesChanged(func() { go windows.WithCOM(func() { s.licensesChanged(ctx) }) })
	})
	return s.ctx, s.err
}

func (s *msStore) refresh(ctx *windows.StoreContext) error {
	as, err := ctx.AddOns()
	if err != nil {
		return err
	}
	s.mu.Lock()
	for _, a := range as {
		s.addOns[a.StoreID] = a
	}
	s.mu.Unlock()
	return nil
}

// find is the add-on of a product ID (its offer token) or Store ID.
func (s *msStore) find(id string) (windows.StoreAddOn, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if a, ok := s.addOns[id]; ok {
		return a, true
	}
	for _, a := range s.addOns {
		if a.OfferToken == id {
			return a, true
		}
	}
	return windows.StoreAddOn{}, false
}

func productIDOf(a windows.StoreAddOn) string {
	if a.OfferToken != "" {
		return a.OfferToken
	}
	return a.StoreID
}

func kindOf(a windows.StoreAddOn) Kind {
	switch {
	case strings.HasSuffix(a.Kind, "Consumable"):
		return KindConsumable
	case a.Subscription:
		return KindAutoRenewable
	}
	return KindNonConsumable
}

func period(unit string, n int) Period {
	u := map[string]PeriodUnit{"day": PeriodUnitDay, "week": PeriodUnitWeek, "month": PeriodUnitMonth, "year": PeriodUnitYear}[strings.ToLower(unit)]
	return Period{Unit: u, Count: n}
}

func (s *msStore) product(a windows.StoreAddOn, id string) Product {
	p := Product{Id: id, Kind: kindOf(a), Title: a.Title, Description: a.Description, Price: a.Price, PriceMicros: int(a.Micros), Currency: a.Currency}
	if a.Subscription {
		sub := &Subscription{Group: productIDOf(a), Period: period(a.PeriodUnit, a.PeriodCount)}
		if a.TrialCount > 0 {
			sub.IntroEligible = !a.Owned
			sub.Offers = []Offer{{Type: OfferTypeIntroductory, Phases: []Phase{{Price: "Free", Currency: a.Currency, Period: period(a.TrialUnit, a.TrialCount), Cycles: 1, Mode: ModeFreeTrial}}}}
		}
		p.Subscription = sub
	}
	return p
}

// transaction is a purchase of a: a consumable's under a new tracking ID
// (what its fulfillment reports), the others' under the add-on's Store ID.
func (s *msStore) transaction(a windows.StoreAddOn, id string) Transaction {
	if id == "" {
		id = productIDOf(a)
	}
	t := Transaction{Id: a.StoreID, ProductId: id, Kind: kindOf(a), Quantity: 1, State: StatePurchased, AutoRenews: a.Subscription,
		Storefront: windows.UserCountry(), Verification: Verification{Token: a.StoreID}}
	if t.Kind == KindConsumable {
		t.Id = windows.NewTrackingID()
	}
	return t
}

func (s *msStore) Available() (bool, error) { return true, nil }

func (s *msStore) Declare(items []Item) error {
	s.mu.Lock()
	for _, it := range items {
		s.kinds[it.Id] = it.Kind
	}
	s.mu.Unlock()
	return nil
}

func (s *msStore) Products(_ context.Context, items []Item) ([]Product, error) {
	var out []Product
	err := s.com(func(ctx *windows.StoreContext) error {
		if err := s.refresh(ctx); err != nil {
			return err
		}
		for _, it := range items {
			if a, ok := s.find(it.Id); ok {
				out = append(out, s.product(a, it.Id))
			}
		}
		return nil
	})
	return out, err
}

func (s *msStore) Buy(_ context.Context, it Item, _ Options) (r Result, err error) {
	err = s.com(func(ctx *windows.StoreContext) error {
		a, ok := s.find(it.Id)
		if !ok {
			if err := s.refresh(ctx); err != nil {
				return err
			}
			if a, ok = s.find(it.Id); !ok {
				return ErrUnknownProduct
			}
		}
		status, err := ctx.Purchase(a.StoreID)
		switch status {
		case windows.StorePurchased:
			s.mu.Lock()
			s.bought[a.StoreID] = true
			s.mu.Unlock()
			t := s.transaction(a, it.Id)
			t.Date = int(time.Now().UnixNano())
			r = Result{Status: StatusBought, Transaction: &t}
			return nil
		case windows.StoreAlreadyPurchased:
			return ErrAlreadyOwned
		case windows.StoreNotPurchased:
			r = Result{Status: StatusCancelled}
			return nil
		case windows.StoreNetworkError:
			return errors.New("purchase: the Microsoft Store can't be reached")
		}
		if err == nil {
			err = errors.New("purchase: the Microsoft Store failed")
		}
		return err
	})
	return r, err
}

func (s *msStore) Finish(_ context.Context, t Transaction) error {
	if t.Kind != KindConsumable {
		return nil // a durable's license is its own
	}
	return s.com(func(ctx *windows.StoreContext) error {
		tracking := t.Id
		if !strings.HasPrefix(tracking, "{") {
			tracking = windows.NewTrackingID()
		}
		status, err := ctx.Fulfill(storeIDOf(t.Verification.Token), uint32(max(t.Quantity, 1)), tracking)
		if status != 0 && err == nil {
			err = errors.New("purchase: the Microsoft Store didn't take the fulfillment")
		}
		return err
	})
}

func (s *msStore) entitlements(ctx *windows.StoreContext) ([]Transaction, error) {
	ls, err := ctx.Licenses()
	if err != nil {
		return nil, err
	}
	out := []Transaction{}
	for _, l := range ls {
		s.mu.Lock()
		a, ok := s.addOns[storeIDOf(l.SkuStoreID)]
		s.mu.Unlock()
		if !l.Active || !ok || kindOf(a) == KindConsumable {
			continue
		}
		t := s.transaction(a, "")
		if !l.Expires.IsZero() {
			t.Expires = int(l.Expires.UnixNano())
		}
		out = append(out, t)
	}
	return out, nil
}

func (s *msStore) Entitlements(context.Context) (out []Transaction, err error) {
	err = s.com(func(ctx *windows.StoreContext) error {
		out, err = s.entitlements(ctx)
		return err
	})
	return out, err
}

func (s *msStore) Restore(context.Context) (out []Transaction, err error) {
	err = s.com(func(ctx *windows.StoreContext) error {
		s.licensesChanged(ctx)
		out, err = s.entitlements(ctx)
		return err
	})
	return out, err
}

func (s *msStore) Statuses(_ context.Context, group string) (out []SubscriptionStatus, err error) {
	err = s.com(func(ctx *windows.StoreContext) error {
		ls, err := ctx.Licenses()
		if err != nil {
			return err
		}
		for _, l := range ls {
			s.mu.Lock()
			a, ok := s.addOns[storeIDOf(l.SkuStoreID)]
			s.mu.Unlock()
			if !ok || !a.Subscription || (l.OfferToken != group && a.StoreID != group) {
				continue
			}
			st := SubscriptionStatus{ProductId: productIDOf(a), State: SubscriptionStateSubscribed, WillRenew: l.Active, Transaction: s.transaction(a, "")}
			if !l.Active {
				st.State = SubscriptionStateExpired
			}
			if !l.Expires.IsZero() {
				st.Renewal = int(l.Expires.UnixNano())
				st.Transaction.Expires = st.Renewal
			}
			out = append(out, st)
		}
		return nil
	})
	return out, err
}

func (s *msStore) ManageSubscriptions(context.Context, string) error {
	native.Post(func() { windows.OpenURL("https://account.microsoft.com/services") })
	return nil
}

func (s *msStore) RequestRefund(context.Context, string) (bool, error) {
	return false, errors.New("unsupported: the Microsoft Store's refunds are asked for on its website")
}

func (s *msStore) RedeemCode(context.Context) error {
	native.Post(func() { windows.OpenURL("https://www.microsoft.com/redeem") })
	return nil
}

func (s *msStore) Storefront(context.Context) (Storefront, error) {
	return Storefront{Country: windows.UserCountry()}, nil
}

func (s *msStore) Events(ctx context.Context, f func(Event)) error {
	s.mu.Lock()
	s.next++
	id := s.next
	s.readers[id] = f
	kept := s.kept
	s.kept = nil
	s.mu.Unlock()
	native.Post(func() {
		for _, e := range kept {
			f(e)
		}
	})
	go windows.WithCOM(func() { s.context() })
	<-ctx.Done()
	s.mu.Lock()
	delete(s.readers, id)
	s.mu.Unlock()
	return ctx.Err()
}

// licensesChanged compares the licenses with those seen: one new is a
// purchase outside a buy (another PC, a renewal's new period), a
// durable's gone is revoked (refunded).
func (s *msStore) licensesChanged(ctx *windows.StoreContext) {
	ls, err := ctx.Licenses()
	if err != nil {
		return
	}
	s.refresh(ctx)
	now := map[string]windows.StoreAddOnLicense{}
	for _, l := range ls {
		if l.Active {
			now[storeIDOf(l.SkuStoreID)] = l
		}
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	for id, l := range now {
		old, seen := s.active[id]
		a, ok := s.addOns[id]
		if !ok {
			continue
		}
		if s.bought[id] {
			delete(s.bought, id)
			continue
		}
		if !seen || !l.Expires.Equal(old.Expires) {
			t := s.transaction(a, "")
			t.Date = int(time.Now().UnixNano())
			if !l.Expires.IsZero() {
				t.Expires = int(l.Expires.UnixNano())
			}
			s.give(t)
		}
	}
	for id := range s.active {
		if _, still := now[id]; still {
			continue
		}
		if a, ok := s.addOns[id]; ok && kindOf(a) == KindNonConsumable {
			t := s.transaction(a, "")
			t.State, t.Revoked = StateRevoked, int(time.Now().UnixNano())
			s.give(t)
		}
	}
	s.active = now
}

// give gives a transaction to events(), or keeps it for the first. s.mu
// held.
func (s *msStore) give(t Transaction) {
	e := Event{Type: EventTypeTransactionUpdated, Transaction: &t}
	if len(s.readers) == 0 {
		s.kept = append(s.kept, e)
		return
	}
	for _, f := range s.readers {
		native.Post(func() { f(e) })
	}
}
