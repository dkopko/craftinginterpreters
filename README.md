This repo is a fork of the clox interpreter from the in-progress book
"[Crafting Interpreters][]".  This fork is to be a proof-of-concept of
tripartite data structures and continuous garbage collection.

[crafting interpreters]: http://craftinginterpreters.com

## Building Stuff
First, build the `klox_integration` branch of the "[CB Project][]".

[CB Project]: https://github.com/dkopko/cb

Next:

```sh
$ cd c
# (Modify the paths in Makefile to point to the built CB project.)
$ make
```
