Make this app a shopping cart.

- The root component is `Shop`, and the `app` block shows it.
- Three products, each a row with its name and price and a button
  titled `Add <name>`: Tea at 4 dollars, Cake at 6 and Jam at 3.
- The cart lists what's in it, one text per product added, as
  `<name> × <quantity>` (adding a product again raises its quantity),
  in the order first added, with a button titled `Remove <name>` that
  takes one away (the line goes at 0).
- A text says `Total: $N`, the cart's total in whole dollars.
