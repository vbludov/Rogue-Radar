# Host regression tests

Run the dependency-free C++11 model and radio lifecycle suites from the repository root:

```sh
sh tests/run_host_tests.sh
```

The script uses `CXX` when set, defaults to `g++`, and builds in a temporary directory outside the checkout.