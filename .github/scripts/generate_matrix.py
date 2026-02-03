import os
import json


def get_presets():
    def p(name, replay=False):
        return {"preset": name, "tools": True, "extras": True, "replay": replay}

    generals = [
        p("vc6"),
        p("vc6-profile"),
        p("vc6-debug"),
        p("win32"),
        p("win32-profile"),
        p("win32-debug"),
        p("win32-vcpkg"),
        p("win32-vcpkg-profile"),
        p("win32-vcpkg-debug"),
    ]

    # replay=True for retail-compatible builds that need replay testing
    generalsmd = [
        p("vc6", replay=True),
        p("vc6-profile"),
        p("vc6-debug"),
        p("vc6-releaselog", replay=True),
        p("win32"),
        p("win32-profile"),
        p("win32-debug"),
        p("win32-vcpkg"),
        p("win32-vcpkg-profile"),
        p("win32-vcpkg-debug"),
    ]

    return {
        "presets_generals": generals,
        "presets_generalsmd": generalsmd,
    }


if __name__ == "__main__":
    matrix_map = get_presets()

    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a") as f:
            for key, value in matrix_map.items():
                f.write(f"{key}={json.dumps(value)}\n")
    else:
        for key, value in matrix_map.items():
            print(f"{key}:")
            print(json.dumps(value, indent=2))
