capnp compile "$1" --src-prefix "$(dirname "$1")" -oc++:"$(dirname "$2")" >&2 