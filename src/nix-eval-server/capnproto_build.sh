
# echo $@ > test.txt
echo BBBBBBBBBBBBBB capnp compile "$1" --src-prefix "$(dirname "$1")" -oc++:"$(dirname "$2")" >&2 
capnp compile "$1" --src-prefix "$(dirname "$1")" -oc++:"$(dirname "$2")" >&2 