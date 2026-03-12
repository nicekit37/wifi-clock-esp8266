import re
import subprocess
from datetime import date
from pathlib import Path
import sys


def run(cmd: list[str]) -> None:
    """Run a shell command and fail fast on error."""
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"[release] command failed: {' '.join(cmd)}")
        print(result.stdout)
        print(result.stderr)
        sys.exit(result.returncode)


def get_project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def read_firmware_version(root: Path) -> str:
    header = root / "include" / "main.h"
    text = header.read_text(encoding="utf-8")
    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', text)
    if not m:
        print("[release] Не удалось найти FIRMWARE_VERSION в include/main.h")
        sys.exit(1)
    return m.group(1)


def update_changelog(root: Path, version: str, description: str) -> None:
    changelog = root / "CHANGELOG.md"
    today = date.today().isoformat()

    new_section_lines = [
        f"## [{version}] - {today}",
        "",
        "### Changes",
        f"- {description}",
        "",
    ]
    new_section = "\n".join(new_section_lines)

    if changelog.exists():
        existing = changelog.read_text(encoding="utf-8")
        if f"[{version}]" in existing:
            # Уже есть секция для этой версии – не дублируем
            return
        content = "# Changelog\n\n" + new_section + existing.lstrip("# \n")
    else:
        content = "# Changelog\n\n" + new_section

    changelog.write_text(content, encoding="utf-8")


def main() -> None:
    root = get_project_root()
    description = " ".join(sys.argv[1:]).strip()
    if not description:
        description = "Release firmware"

    version = read_firmware_version(root)
    tag = f"v{version}"

    print(f"[release] Версия: {version}")
    print(f"[release] Тег: {tag}")

    # Обновляем CHANGELOG
    update_changelog(root, version, description)

    # Добавляем файлы и коммитим
    run(["git", "-C", str(root), "add", "CHANGELOG.md", "include/main.h", "src", ".ai-factory", "AGENTS.md", "platformio.ini"])
    run(["git", "-C", str(root), "commit", "-m", f"chore: release firmware {version}"])

    # Создаём тег и пушим
    run(["git", "-C", str(root), "tag", "-a", tag, "-m", f"WiFi Clock firmware {version}"])
    run(["git", "-C", str(root), "push"])
    run(["git", "-C", str(root), "push", "origin", tag])

    print("[release] Готово.")


if __name__ == "__main__":
    main()

