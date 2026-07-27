# JPXStream AST Diagram

`jpx_stream_ast.dot` is a Graphviz DOT diagram for the
`modules/jpxstream/jpx_stream.proto` schema.  It shows:

- the proto AST grouped by JPEG 2000 marker responsibility
- how each group maps to serializer output
- the CVE-2022-24107 bug path from SIZ tile dimensions to Xpdf's overflow

Render with Graphviz:

```sh
cd /home/parkle/master-thesis-research-logs/research/schema/pdf-proto/modules/jpxstream/diagram
dot -Tsvg jpx_stream_ast.dot -o jpx_stream_ast.svg
dot -Tpdf jpx_stream_ast.dot -o jpx_stream_ast.pdf
```
