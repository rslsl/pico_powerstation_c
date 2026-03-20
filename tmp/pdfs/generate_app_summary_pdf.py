from pathlib import Path


PAGE_W = 612
PAGE_H = 792
LEFT = 46
RIGHT = 46
TOP = 50
BOTTOM = 44


def pdf_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")


def wrap_text(text: str, max_chars: int) -> list[str]:
    words = text.split()
    if not words:
        return [""]
    lines = []
    current = words[0]
    for word in words[1:]:
        candidate = f"{current} {word}"
        if len(candidate) <= max_chars:
            current = candidate
        else:
            lines.append(current)
            current = word
    lines.append(current)
    return lines


def text_block(lines: list[tuple[str, int, int]], x: int, y: int, size: int, leading: int) -> str:
    parts = ["BT", f"/F1 {size} Tf", f"{leading} TL", f"1 0 0 1 {x} {y} Tm"]
    first = True
    for text, dx, dy in lines:
        if first:
            parts.append(f"({pdf_escape(text)}) Tj")
            first = False
        else:
            parts.append(f"{dx} {dy} Td")
            parts.append(f"({pdf_escape(text)}) Tj")
    parts.append("ET")
    return "\n".join(parts)


def build_content() -> tuple[bytes, int]:
    y = PAGE_H - TOP
    commands: list[str] = []

    def add_line(text: str, size: int = 11, leading: int = 14, gap: int | None = None) -> None:
        nonlocal y
        if gap is None:
            gap = leading
        commands.append(text_block([(text, 0, 0)], LEFT, y, size, leading))
        y -= gap

    def add_wrapped(text: str, size: int = 11, leading: int = 13, max_chars: int = 80, after: int = 0) -> None:
        nonlocal y
        wrapped = wrap_text(text, max_chars)
        first = True
        for line in wrapped:
            commands.append(text_block([(line, 0, 0)], LEFT, y, size, leading))
            y -= leading
            first = False
        y -= after

    def add_bullet(text: str, max_chars: int = 74, size: int = 10, leading: int = 12) -> None:
        nonlocal y
        wrapped = wrap_text(text, max_chars)
        bullet_x = LEFT + 4
        text_x = LEFT + 18
        commands.append(text_block([("-", 0, 0)], bullet_x, y, size, leading))
        commands.append(text_block([(wrapped[0], 0, 0)], text_x, y, size, leading))
        y -= leading
        for line in wrapped[1:]:
            commands.append(text_block([(line, 0, 0)], text_x, y, size, leading))
            y -= leading

    add_line("PowerStation Firmware Summary", size=20, leading=22, gap=24)
    add_line("RP2040 portable power-station BMS and local UI", size=12, leading=14, gap=22)

    add_line("What it is", size=13, leading=15, gap=16)
    add_wrapped(
        "Embedded firmware for an RP2040-based PowerStation device with battery management, "
        "power-output control, protection logic, event logging, and an onboard ST7789 UI.",
        max_chars=88,
    )
    add_wrapped(
        "The repo shows a dual-core design where Core0 runs sensing and control loops while Core1 "
        "renders the display. Network or cloud services were not found in repo.",
        max_chars=88,
        after=6,
    )

    add_line("Who it's for", size=13, leading=15, gap=16)
    add_wrapped(
        "Primary persona: an embedded developer or device integrator building or maintaining this "
        "RP2040 power-station/BMS firmware. End-user persona details: Not found in repo.",
        max_chars=88,
        after=6,
    )

    add_line("What it does", size=13, leading=15, gap=16)
    for bullet in [
        "Monitors pack voltage/current with 2 INA226 sensors and cell voltages with an INA3221 through a TCA9548A I2C mux.",
        "Reads battery and DC-USB temperatures from LM75A sensors and drives fan behavior with fail-safe sensor checks.",
        "Runs BMS estimation and health logic including OCV, EKF SOC estimation, Rint modeling, SOH tracking, and runtime prediction.",
        "Applies power policies for normal, charge-only, isolated, and fault-latched modes across DC out, USB PD, fan, and charge paths.",
        "Performs startup diagnostics, safety checks, alarm handling, watchdog servicing, and emergency-off behavior.",
        "Shows a local multi-screen UI for main status, battery, diagnostics, ports, stats, events, history, and I2C scan views.",
    ]:
        add_bullet(bullet)
    y -= 4

    add_line("How it works", size=13, leading=15, gap=16)
    for bullet in [
        "Entrypoint: src/main.c latches system power, initializes peripherals, validates startup state, selects boot policy, and launches Core1.",
        "Sensors/data flow: drivers in src/drivers read I2C sensors and SPI display hardware; src/bms/battery.c aggregates raw readings into a shared Battery state.",
        "Algorithms/services: src/bms contains OCV, EKF, Rint, SOH, predictor, stats, logger, and flash NVM modules used by the battery coordinator and save paths.",
        "Control path: src/app/protection.c and src/app/power_control.c translate battery state into alarms, safe mode, relay changes, and buzzer events.",
        "UI/render path: Core0 updates snapshots under spin locks; Core1 renders only from snapshot data via src/app/ui.c and src/app/display.c to the ST7789 screen.",
    ]:
        add_bullet(bullet)
    y -= 4

    add_line("How to run", size=13, leading=15, gap=16)
    for bullet in [
        "Install the repo prerequisites from BUILD.md: Pico SDK 2.0+, CMake 3.13+, arm-none-eabi-gcc 12+, and Python 3.",
        "Set PICO_SDK_PATH to your local pico-sdk checkout.",
        "Build: mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4",
        "Flash build/pico_powerstation.uf2 to the RPI-RP2 boot device. Windows-specific flashing steps: Not found in repo.",
    ]:
        add_bullet(bullet, max_chars=76)

    footer = "Source basis: CMakeLists.txt, BUILD.md, src/main.c, src/config.h, src/bms/*, src/app/*, ui_assets/README.md."
    commands.append(text_block([(footer, 0, 0)], LEFT, BOTTOM, 8, 10))

    return "\n".join(commands).encode("latin-1", errors="replace"), y


def make_pdf(content: bytes) -> bytes:
    objects: list[bytes] = []

    def add_object(body: bytes) -> int:
        objects.append(body)
        return len(objects)

    catalog_id = add_object(b"<< /Type /Catalog /Pages 2 0 R >>")
    pages_id = add_object(b"<< /Type /Pages /Count 1 /Kids [3 0 R] >>")
    page_id = add_object(
        f"<< /Type /Page /Parent {pages_id} 0 R /MediaBox [0 0 {PAGE_W} {PAGE_H}] "
        f"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>".encode("ascii")
    )
    font_id = add_object(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    stream = b"<< /Length " + str(len(content)).encode("ascii") + b" >>\nstream\n" + content + b"\nendstream"
    content_id = add_object(stream)

    assert [catalog_id, pages_id, page_id, font_id, content_id] == [1, 2, 3, 4, 5]

    pdf = bytearray(b"%PDF-1.4\n%\xE2\xE3\xCF\xD3\n")
    offsets = [0]
    for i, obj in enumerate(objects, start=1):
        offsets.append(len(pdf))
        pdf.extend(f"{i} 0 obj\n".encode("ascii"))
        pdf.extend(obj)
        pdf.extend(b"\nendobj\n")

    xref_offset = len(pdf)
    pdf.extend(f"xref\n0 {len(objects) + 1}\n".encode("ascii"))
    pdf.extend(b"0000000000 65535 f \n")
    for offset in offsets[1:]:
        pdf.extend(f"{offset:010d} 00000 n \n".encode("ascii"))
    pdf.extend(
        f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\nstartxref\n{xref_offset}\n%%EOF\n".encode("ascii")
    )
    return bytes(pdf)


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    out_dir = root / "output" / "pdf"
    out_dir.mkdir(parents=True, exist_ok=True)
    pdf_path = out_dir / "pico_powerstation_app_summary.pdf"
    content, final_y = build_content()
    pdf_path.write_bytes(make_pdf(content))
    print(pdf_path)
    print(f"final_y={final_y}")


if __name__ == "__main__":
    main()
