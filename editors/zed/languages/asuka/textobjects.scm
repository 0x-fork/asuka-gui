(function_declaration
  body: (_
    "{"
    (_)* @function.inside
    "}")) @function.around

(function_declaration
  value: (_) @function.inside) @function.around

(lambda_expression
  body: (_
    "{"
    (_)* @function.inside
    "}")) @function.around

(lambda_expression
  body: (_) @function.inside) @function.around

(on_handler
  body: (_
    "{"
    (_)* @function.inside
    "}")) @function.around

[
  (component_declaration body: (_ "{" (_)* @class.inside "}"))
  (screen_declaration body: (_ "{" (_)* @class.inside "}"))
  (store_declaration body: (_ "{" (_)* @class.inside "}"))
  (worker_declaration body: (_ "{" (_)* @class.inside "}"))
  (struct_declaration body: (_ "{" (_)* @class.inside "}"))
  (enum_declaration body: (_ "{" (_)* @class.inside "}"))
  (interface_declaration body: (_ "{" (_)* @class.inside "}"))
  (extend_declaration body: (_ "{" (_)* @class.inside "}"))
  (test_declaration body: (_ "{" (_)* @class.inside "}"))
] @class.around

(comment)+ @comment.around
