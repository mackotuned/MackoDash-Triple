import argparse
import csv
import struct
import zlib
from pathlib import Path


HEADER = struct.Struct("<4sBBHHHIII")
RECORD = struct.Struct("<BBHII10IHHHhhHhHhHHhh8HHHHI")
FIELDS = [
    "version", "gear", "record_size", "uptime_ms", "received_frame_mask",
    *[f"frame_{0x660 + index:03x}_time_ms" for index in range(10)],
    "rpm", "speed_tenths_kph", "battery_mv", "intake_temp_tenths_c",
    "coolant_temp_tenths_c", "throttle_tenths_percent",
    "map_gauge_tenths_kpa", "injector_duration_raw", "ignition_angle_deg",
    "afr_thousandths", "knock_count", "cam_angle_target_deg",
    "cam_angle_actual_deg", *[f"analog_{index + 1}_mv" for index in range(8)],
    "ethanol_frequency_hz", "ethanol_duty_hundredths_percent",
    "ethanol_content_tenths_percent", "crc32",
]


def crc_valid(data: bytes) -> bool:
    return zlib.crc32(data[:-4]) == int.from_bytes(data[-4:], "little")


def export(source: Path, destination: Path) -> int:
    with source.open("rb") as input_file:
        raw_header = input_file.read(HEADER.size)
        if len(raw_header) != HEADER.size or not crc_valid(raw_header):
            raise ValueError("invalid or truncated log header")
        magic, version, log_name, header_size, record_size, session, start_ms, _, _ = (
            HEADER.unpack(raw_header)
        )
        if magic != b"MDHL" or version != 1 or header_size != HEADER.size or record_size != RECORD.size:
            raise ValueError("unsupported Hondata log format")

        count = 0
        with destination.open("w", newline="", encoding="utf-8") as output_file:
            writer = csv.writer(output_file)
            writer.writerow(["session", session, "log_name", log_name,
                             "start_uptime_ms", start_ms])
            writer.writerow(FIELDS)
            while raw_record := input_file.read(RECORD.size):
                if len(raw_record) != RECORD.size:
                    raise ValueError(f"truncated record after row {count}")
                if not crc_valid(raw_record):
                    raise ValueError(f"CRC failure at row {count}")
                values = RECORD.unpack(raw_record)
                if values[0] != 1 or values[2] != RECORD.size:
                    raise ValueError(f"unsupported record at row {count}")
                writer.writerow(values)
                count += 1
    return count


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Validate and export a MackoDash Hondata SD log to CSV")
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path, nargs="?")
    args = parser.parse_args()
    destination = args.destination or args.source.with_suffix(".csv")
    count = export(args.source, destination)
    print(f"Exported {count} records to {destination}")


if __name__ == "__main__":
    main()