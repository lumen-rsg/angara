; Functions
(function_declaration
  body: (block) @function.inner) @function.outer

(lambda_expression
  body: (block) @function.inner) @function.outer

; Classes
(class_declaration) @class.outer

; Blocks
(block) @block.outer

; Parameters
(parameters
  (parameter) @parameter.inner)

(arguments
  (argument) @parameter.inner)

; Comments
(comment) @comment.outer

; Conditionals
(if_statement) @conditional.outer

; Loops
(for_statement) @loop.outer
(while_statement) @loop.outer
