import os

Import("env")

version = os.environ.get("APP_VERSION", "0.1.0")
if not version or len(version) > 31 or '"' in version or "\\" in version:
    raise ValueError("APP_VERSION must be a non-empty version string")

env.Append(CPPDEFINES=[("APP_VERSION", env.StringifyMacro(version))])
