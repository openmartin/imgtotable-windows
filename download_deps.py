#!/usr/bin/env python3
"""Download this project's desktop SDKs. Requires Python 3.12+; no global installs."""

import argparse
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from urllib.parse import urlparse
import venv
import zipfile


PROJECT_ROOT = Path(__file__).resolve().parent
AQT_VERSION = "3.3.0"


def version(value):
    if not re.fullmatch(r"\d+\.\d+\.\d+", value):
        raise argparse.ArgumentTypeError("Use an exact version such as 6.8.3")
    return value


def mirror_url(value):
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.netloc or parsed.query or parsed.fragment:
        raise argparse.ArgumentTypeError("Use an HTTPS mirror root, e.g. https://mirrors.aliyun.com/qt")
    return value.rstrip("/")


def command_text(command):
    # Windows examples are printed for PowerShell, including paths with spaces.
    if sys.platform == "win32":
        return "& " + " ".join("'" + str(item).replace("'", "''") + "'" for item in command)
    return shlex.join(str(item) for item in command)


def run(command, cwd=None):
    print("+ " + command_text(command), flush=True)
    subprocess.run([str(item) for item in command], cwd=cwd, check=True)


def platform_settings(requested_arch):
    machine = platform.machine().lower()
    if sys.platform == "win32":
        if machine not in ("amd64", "x86_64") or requested_arch not in ("auto", "x86_64"):
            raise RuntimeError("Windows downloads currently support x64 hosts/targets only.")
        return "windows", "win64_msvc2022_64", "msvc2022_64", "win-x64", "x86_64"
    if sys.platform == "darwin":
        arch = requested_arch
        if arch == "auto":
            arch = "arm64" if machine == "arm64" else "x86_64"
            # A Python interpreter running under Rosetta reports x86_64.
            translated = subprocess.run(
                ["/usr/sbin/sysctl", "-in", "sysctl.proc_translated"],
                capture_output=True, text=True, check=False,
            )
            if translated.stdout.strip() == "1":
                arch = "arm64"
        if arch != "arm64":
            raise RuntimeError("macOS targets support Apple Silicon (arm64) only; Intel Mac is no longer supported.")
        return "mac", "clang_64", "macos", "osx-arm64", arch
    raise RuntimeError("Only Windows x64 and macOS arm64 are supported.")


def install_qt(root, qt_version, host, qt_arch, qt_directory, qt_mirror=None):
    prefix = root / "Qt" / qt_version / qt_directory
    marker = prefix / ".imgtotable-download-complete"
    config = prefix / "lib/cmake/Qt6/Qt6Config.cmake"
    if marker.is_file() and config.is_file():
        print(f"Qt already installed: {prefix}")
        return prefix

    environment = root / ".aqt-venv"
    python = environment / ("Scripts/python.exe" if host == "windows" else "bin/python")
    if not python.is_file():
        venv.EnvBuilder(with_pip=True).create(environment)
    run([python, "-m", "pip", "install", "--disable-pip-version-check", f"aqtinstall=={AQT_VERSION}"])
    # qtbase contains Core/Gui/Widgets. Do not download QML, Qt Quick or WebEngine.
    # aqt also handles archive checksums and Qt's relocatable installation paths.
    command = [python, "-m", "aqt", "install-qt", host, "desktop", qt_version, qt_arch,
               "--outputdir", root / "Qt", "--archives", "qtbase"]
    if qt_mirror:
        command.extend(["--base", qt_mirror])
    print("aqt does not show download progress; a quiet log can mean a slow download.", flush=True)
    run(command, cwd=root)
    if not config.is_file():
        raise RuntimeError(f"Qt download finished but Qt6Config.cmake is missing: {config}")
    marker.write_text(f"Qt {qt_version}, {qt_arch}, qtbase\n", encoding="utf-8")
    return prefix


def install_onnx(root, onnx_version, package):
    name = f"onnxruntime-{package}-{onnx_version}"
    destination = root / name
    library = "onnxruntime.lib" if package == "win-x64" else "libonnxruntime.dylib"
    required = ["include/onnxruntime_cxx_api.h", "lib/" + library]
    if package == "win-x64":
        required.append("lib/onnxruntime.dll")
    if all((destination / item).is_file() for item in required):
        print(f"ONNX Runtime already installed: {destination}")
        return destination
    if destination.exists():
        raise RuntimeError(f"Incomplete SDK directory; move it aside and retry: {destination}")

    suffix = ".zip" if package == "win-x64" else ".tgz"
    url = f"https://github.com/microsoft/onnxruntime/releases/download/v{onnx_version}/{name}{suffix}"
    print(f"Downloading {url}", flush=True)
    # Stage in the destination filesystem. Failures never leave a half-installed SDK.
    with tempfile.TemporaryDirectory(prefix=".onnx-download-", dir=root) as temporary:
        temporary = Path(temporary)
        archive = temporary / (name + suffix)
        request = urllib.request.Request(url, headers={"User-Agent": "ImgToTable-dependency-downloader"})
        with urllib.request.urlopen(request, timeout=60) as response, archive.open("wb") as output:
            shutil.copyfileobj(response, output)
        extraction = temporary / "extracted"
        extraction.mkdir()
        if suffix == ".zip":
            with zipfile.ZipFile(archive) as archive_file:
                for member in archive_file.infolist():
                    path = (extraction / member.filename).resolve()
                    if not path.is_relative_to(extraction.resolve()):
                        raise RuntimeError("Unsafe path in ONNX Runtime archive")
                archive_file.extractall(extraction)
        else:
            with tarfile.open(archive, "r:gz") as archive_file:
                archive_file.extractall(extraction, filter="data")
        extracted = extraction / name
        if not all((extracted / item).is_file() for item in required):
            raise RuntimeError("Downloaded archive does not contain the expected C/C++ SDK layout")
        extracted.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-version", type=version, default="6.8.3",
                        help="Qt version, default: 6.8.3 (MSVC 2022 / macOS kit)")
    parser.add_argument("--onnx-version", type=version, default="1.30.0",
                        help="ONNX Runtime version, default: 1.30.0")
    parser.add_argument("--qt-mirror", type=mirror_url,
                        help="Qt mirror root; default: download.qt.io automatic mirror selection")
    parser.add_argument("--destination", type=Path, default=PROJECT_ROOT / "third_party",
                        help="SDK directory, default: third_party next to this script")
    parser.add_argument("--arch", choices=("auto", "arm64", "x86_64"), default="auto",
                        help="Target architecture: macOS arm64 only; Windows x86_64 only")
    parser.add_argument("--only", choices=("qt", "onnx"), help="Download just one SDK")
    parser.add_argument("--dry-run", action="store_true", help="Print plan without downloading or writing files")
    args = parser.parse_args()
    if sys.version_info < (3, 12):
        parser.error("Python 3.12 or newer is required")
    host, qt_arch, qt_directory, package, arch = platform_settings(args.arch)
    root = args.destination.expanduser().resolve()
    qt_prefix = root / "Qt" / args.qt_version / qt_directory
    onnx_root = root / f"onnxruntime-{package}-{args.onnx_version}"
    print(f"Target: {host} {arch}\nQt: {qt_prefix}\nONNX Runtime CPU: {onnx_root}")
    print(f"Qt download mirror: {args.qt_mirror or 'https://download.qt.io (automatic)'}")
    if not args.dry_run:
        root.mkdir(parents=True, exist_ok=True)
        if args.only != "onnx":
            install_qt(root, args.qt_version, host, qt_arch, qt_directory, args.qt_mirror)
        if args.only != "qt":
            install_onnx(root, args.onnx_version, package)
    command = ["cmake", "-S", PROJECT_ROOT, "-B", PROJECT_ROOT / "build", "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={qt_prefix.as_posix()}",
               f"-DONNXRUNTIME_ROOT={onnx_root.as_posix()}"]
    if host == "mac":
        command.append(f"-DCMAKE_OSX_ARCHITECTURES={arch}")
        command.append("-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0")
    print("\nCMake command (requires both SDKs, CMake/Ninja and the platform compiler):")
    print(command_text(command))
    print(command_text(["cmake", "--build", PROJECT_ROOT / "build"]))
    print("Qt Creator is installed separately; register the Qt kit using this Qt prefix.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nDownload interrupted. Run the same command to retry.", file=sys.stderr)
        sys.exit(130)
