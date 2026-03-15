#!/usr/bin/env python3
"""
CSV Processor
Reads CSV data (without headers) from a file or STDIN,
and writes processed output to a file or STDOUT.
"""

import argparse
import csv
import sys
from pathlib import Path


def process_line(fields: list[str]) -> None:
    """
    Process a single parsed CSV line.
    Receives the contents of one CSV row as an array of strings.
    To be implemented in future additions.
    """
    pass


def make_output_writer(output_stream):
    """
    Returns a print()-compatible function that writes to the given output stream.
    Supports all the same parameters as the built-in print():
      - *objects  : one or more objects to print
      - sep       : separator between objects (default: ' ')
      - end       : string appended after the last object (default: '\\n')
      - flush     : whether to forcibly flush the stream (default: False)
    """
    def write_output(*objects, sep: str = " ", end: str = "\n", flush: bool = False) -> None:
        print(*objects, sep=sep, end=end, file=output_stream, flush=flush)

    return write_output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Read CSV data from a file or STDIN and write output to a file or STDOUT."
    )
    parser.add_argument(
        "-i", "--input",
        metavar="INPUT_FILE",
        default=None,
        help="Path to the input CSV file. If omitted, reads from STDIN.",
    )
    parser.add_argument(
        "-o", "--output",
        metavar="OUTPUT_FILE",
        default=None,
        help="Path to the output file. If omitted, writes to STDOUT.",
    )
    return parser.parse_args()


def open_input(path: str | None):
    """
    Validate and open the input source.
    Returns an open readable stream, or exits with an error message.
    """
    if path is None:
        return sys.stdin

    input_path = Path(path)

    if not input_path.exists():
        print(f"Error: Input file '{path}' does not exist.", file=sys.stderr)
        sys.exit(1)

    if not input_path.is_file():
        print(f"Error: Input path '{path}' is not a regular file.", file=sys.stderr)
        sys.exit(1)

    if not input_path.is_readable() if hasattr(input_path, "is_readable") else not os.access(path, os.R_OK):
        print(f"Error: Input file '{path}' is not readable.", file=sys.stderr)
        sys.exit(1)

    try:
        return open(input_path, newline="", encoding="utf-8")
    except OSError as e:
        print(f"Error: Cannot open input file '{path}': {e.strerror}", file=sys.stderr)
        sys.exit(1)


def open_output(path: str | None):
    """
    Validate and open the output destination.
    Returns an open writable stream, or exits with an error message.
    Overwrites existing files without notification.
    """
    if path is None:
        return sys.stdout

    output_path = Path(path)
    parent_dir = output_path.parent

    if not parent_dir.exists():
        print(
            f"Error: Output directory '{parent_dir}' does not exist.",
            file=sys.stderr,
        )
        sys.exit(1)

    if not parent_dir.is_dir():
        print(
            f"Error: Output parent path '{parent_dir}' is not a directory.",
            file=sys.stderr,
        )
        sys.exit(1)

    try:
        return open(output_path, "w", encoding="utf-8")
    except OSError as e:
        print(f"Error: Cannot open output file '{path}': {e.strerror}", file=sys.stderr)
        sys.exit(1)


def main() -> None:
    args = parse_args()

    input_stream = open_input(args.input)
    output_stream = open_output(args.output)

    out = make_output_writer(output_stream)

    try:
        reader = csv.reader(input_stream)
        for fields in reader:
            process_line(fields)
            # Example pass-through: write the row to OUTPUT as-is.
            out(",".join(fields))
    finally:
        if input_stream is not sys.stdin:
            input_stream.close()
        if output_stream is not sys.stdout:
            output_stream.close()


if __name__ == "__main__":
    main()
