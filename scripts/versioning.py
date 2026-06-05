import os
import re
import shutil

Import("env")

PROJECT_DIR = env.subst("$PROJECT_DIR")
BUILD_DIR = env.subst("$BUILD_DIR")
VERSION_FILE = os.path.join(PROJECT_DIR, "version.txt")
CONFIG_H = os.path.join(PROJECT_DIR, "src", "config.h")
BVH = os.path.join(PROJECT_DIR, "src", "build_version.h")


def get_semver():
    with open(CONFIG_H, "r") as f:
        content = f.read()
    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
    return m.group(1) if m else "0.0.0"


def read_build():
    if not os.path.exists(VERSION_FILE):
        return 1
    with open(VERSION_FILE, "r") as f:
        parts = f.read().strip().split()
        if len(parts) >= 2:
            try:
                return int(parts[1])
            except ValueError:
                return 1
    return 1


def write_version(semver, build):
    with open(VERSION_FILE, "w") as f:
        f.write(f"{semver} {build}\n")


# At import time: generate build_version.h with current build number
semver = get_semver()
build = read_build()
with open(BVH, "w") as f:
    f.write(f"#pragma once\n#define FIRMWARE_BUILD {build}\n")
print(f"versioning.py: build v{semver} (build #{build})")


# Post-build action: increment build number and rename firmware.bin
def post_build(source, target, env):
    semver = get_semver()
    build = read_build()
    # Increment build counter for next compilation
    write_version(semver, build + 1)
    # Copy firmware.bin to versioned name
    src = str(target[0])
    dst = os.path.join(BUILD_DIR, f"firmware_v{semver}_b{build}.bin")
    if os.path.exists(src):
        shutil.copy2(src, dst)
        print(f"versioning.py: copied -> {dst}")
    else:
        print(f"versioning.py: {src} not found, skipping")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", post_build)
