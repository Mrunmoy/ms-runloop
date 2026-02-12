#!/usr/bin/env python3

import argparse
import os
import shutil
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(SCRIPT_DIR, "build")


def run(cmd, **kwargs):
    print(f">>> {' '.join(cmd)}")
    result = subprocess.run(cmd, **kwargs)
    if result.returncode != 0:
        sys.exit(result.returncode)


def build():
    os.makedirs(BUILD_DIR, exist_ok=True)
    run(["cmake", ".."], cwd=BUILD_DIR)
    run(["make", "-j{}".format(os.cpu_count() or 1)], cwd=BUILD_DIR)


def test():
    run(["ctest", "--output-on-failure"], cwd=BUILD_DIR)


def clean():
    if os.path.isdir(BUILD_DIR):
        shutil.rmtree(BUILD_DIR)
        print(f">>> Removed {BUILD_DIR}")
    else:
        print(">>> Nothing to clean")


def main():
    parser = argparse.ArgumentParser(description="Build the RPC project")
    parser.add_argument("-t", "--test", action="store_true",
                        help="Build and run tests")
    parser.add_argument("-c", "--clean", action="store_true",
                        help="Remove build directory")
    args = parser.parse_args()

    if args.clean:
        clean()
        return

    build()

    if args.test:
        test()


if __name__ == "__main__":
    main()
