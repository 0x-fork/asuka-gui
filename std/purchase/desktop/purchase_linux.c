// asuka/purchase on Linux: no store (macOS runs the package's Swift):
// available is false, and the rest throw notAvailable.
#include "purchase.asuka.h"

purchase_Store *purchase_make_Store(asuka_error *err) {
	asuka_error_declared(err, purchase_PurchaseError_notAvailable);
	return NULL;
}
