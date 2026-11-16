package crash

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/egoist/asuka/internal/crashrun"
)

// An error the app records is a report Follow finds, with the run's
// breadcrumbs and values; deleted, it's gone.
func TestRecordFollowDelete(t *testing.T) {
	t.Setenv("ASUKA_DATA", t.TempDir())
	if crashrun.Start(crashrun.Info{System: "Test", AppVersion: "1.0"}) == "" {
		t.Skip("crashrun didn't start")
	}
	r := reporter{}
	r.Set("screen", "cart")
	r.Breadcrumb("opened the cart")
	r.Record("checkout: no network")
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	got := make(chan Report, 4)
	go r.Follow(ctx, func(x Report) { got <- x })
	select {
	case x := <-got:
		if !strings.Contains(x.Message, "checkout: no network") || x.Kind != "nonfatal" || x.Context.Values["screen"] != "cart" {
			t.Errorf("%+v", x)
		}
		if n := len(x.Context.Breadcrumbs); n == 0 || x.Context.Breadcrumbs[n-1].Message != "opened the cart" {
			t.Errorf("breadcrumbs: %+v", x.Context.Breadcrumbs)
		}
		if err := r.Delete(x.Id); err != nil {
			t.Error(err)
		}
	case <-ctx.Done():
		t.Fatal("no report")
	}
}
