// platform/purchase on the web: Google Play, through the Digital Goods API
// (its details, the user's purchases, consuming) and the Payment Request
// API (a purchase), as a Trusted Web Activity that Play installs has them
// (Chromium on Android and ChromeOS). Elsewhere a browser has no store:
// available is false, and the rest throw notAvailable.
import { Kind, State, Status, Mode, OfferType, PeriodUnit, EventType, PurchaseError } from "./purchase.asuka.js";

const PLAY = "https://play.google.com/billing";

const micros = (v) => Math.round(Number(v || 0) * 1e6);

function period(iso) {
  const m = /^P(\d+)([DWMY])$/.exec(iso || "");
  if (!m) return { unit: PeriodUnit.month, count: 0 };
  return { unit: { D: PeriodUnit.day, W: PeriodUnit.week, M: PeriodUnit.month, Y: PeriodUnit.year }[m[2]], count: Number(m[1]) };
}

function format(price) {
  if (!price) return "";
  try {
    return new Intl.NumberFormat(undefined, { style: "currency", currency: price.currency }).format(Number(price.value));
  } catch {
    return `${price.value} ${price.currency}`;
  }
}

const has = () => typeof window !== "undefined" && "getDigitalGoodsService" in window;

let svc = null;
let svcKnown = false; // the service was asked for and answered
const kinds = new Map(); // product ID → the app's kind
const given = new Set(); // purchase tokens given through events
const kept = [];
const readers = new Set();

async function service() {
  if (!has()) throw new PurchaseError("notAvailable");
  try {
    svc ??= await window.getDigitalGoodsService(PLAY);
  } catch {
    svcKnown = true;
    throw new PurchaseError("notAvailable");
  }
  svcKnown = true;
  if (!svc) throw new PurchaseError("notAvailable");
  return svc;
}

const kindOf = (id, type) => kinds.get(id) ?? (type === "subscription" ? Kind.autoRenewable : Kind.nonConsumable);

const tx = (p, state = State.purchased) => ({
  id: p.purchaseToken, productId: p.itemId, kind: kindOf(p.itemId), quantity: 1, state,
  acknowledged: !!p.acknowledged, verification: { token: p.purchaseToken, verified: false },
});

function give(e) {
  if (readers.size === 0) kept.push(e);
  else for (const r of readers) r(e);
}

// The purchases not acknowledged (left from before, paid outside the
// page): given once, as the page starts and as it's shown again.
async function reconcile() {
  let s;
  try {
    s = await service();
  } catch {
    return;
  }
  for (const p of await s.listPurchases().catch(() => [])) {
    if (p.acknowledged || given.has(p.purchaseToken)) continue;
    given.add(p.purchaseToken);
    give({ type: EventType.transactionUpdated, transaction: tx(p), storefront: {} });
  }
}

function product(d) {
  const p = { id: d.itemId, kind: kindOf(d.itemId, d.type), title: d.title, description: d.description,
    price: format(d.price), priceMicros: micros(d.price?.value), currency: d.price?.currency ?? "", offers: [] };
  if (d.type === "subscription") {
    const phases = [];
    if (d.freeTrialPeriod) {
      phases.push({ price: format({ value: 0, currency: d.price?.currency }), priceMicros: 0, currency: d.price?.currency ?? "",
        period: period(d.freeTrialPeriod), cycles: 1, mode: Mode.freeTrial });
    }
    if (d.introductoryPrice) {
      phases.push({ price: format(d.introductoryPrice), priceMicros: micros(d.introductoryPrice.value), currency: d.introductoryPrice.currency,
        period: period(d.introductoryPricePeriod), cycles: d.introductoryPriceCycles || 1, mode: Mode.payAsYouGo });
    }
    p.subscription = { group: d.itemId, level: 0, period: period(d.subscriptionPeriod),
      offers: phases.length ? [{ type: OfferType.introductory, phases, tags: [] }] : [], introEligible: phases.length > 0 };
  }
  return p;
}

if (has()) {
  reconcile();
  document.addEventListener("visibilitychange", () => document.visibilityState === "visible" && reconcile());
}

export function makeStore() {
  return {
    // Sync: whether the page has Play's service (asked for as it starts).
    available() {
      return has() && typeof PaymentRequest === "function" && (!svcKnown || svc != null);
    },

    declare(items) {
      for (const it of items) kinds.set(it.id, it.kind);
    },

    async products(signal, items) {
      const s = await service();
      for (const it of items) kinds.set(it.id, it.kind);
      const ids = items.map((it) => it.id);
      const got = await s.getDetails(ids);
      return ids.map((id) => got.find((d) => d.itemId === id)).filter(Boolean).map(product);
    },

    async buy(signal, item, options) {
      await service();
      kinds.set(item.id, item.kind);
      const req = new PaymentRequest([{ supportedMethods: PLAY, data: { sku: item.id } }],
        { total: { label: "Total", amount: { currency: "USD", value: "0" } } });
      let res;
      try {
        res = await req.show();
      } catch (e) {
        if (e?.name === "AbortError") return { status: Status.cancelled };
        throw e;
      }
      const token = res.details?.purchaseToken || res.details?.token;
      await res.complete("success");
      if (!token) return { status: Status.cancelled };
      given.add(token);
      return { status: Status.bought, transaction: tx({ itemId: item.id, purchaseToken: token }) };
    },

    async finish(signal, t) {
      const s = await service();
      const token = t.verification?.token;
      if ((kinds.get(t.productId) ?? t.kind) === Kind.consumable) await s.consume(token);
      // The API's first version acknowledged here; the second leaves it
      // to the app's server (the Play Developer API), within 3 days.
      else if (typeof s.acknowledge === "function") await s.acknowledge(token, t.kind === Kind.autoRenewable ? "repeatable" : "onetime");
    },

    async entitlements() {
      return (await (await service()).listPurchases()).map((p) => tx(p));
    },

    async restore() {
      await reconcile();
      return (await (await service()).listPurchases()).map((p) => tx(p));
    },

    async statuses(signal, group) {
      const ps = (await (await service()).listPurchases()).filter((p) => p.itemId === group);
      return ps.map((p) => ({ productId: group, state: 0, willRenew: true, transaction: tx(p) }));
    },

    async manageSubscriptions(signal, id) {
      await service();
      window.open("https://play.google.com/store/account/subscriptions" + (id ? `?sku=${encodeURIComponent(id)}` : ""), "_blank");
    },

    async requestRefund() {
      throw new PurchaseError("notAvailable");
    },

    async redeemCode() {
      await service();
      window.open("https://play.google.com/redeem", "_blank");
    },

    async storefront() {
      await service();
      return { country: new Intl.Locale(navigator.language).maximize().region || "", id: "" };
    },

    async events(signal, f) {
      readers.add(f);
      for (const e of kept.splice(0)) f(e);
      await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
      readers.delete(f);
    },
  };
}
