import argparse
import pathlib
import re
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description="Verify all scanner logging modes with the radio E2E")
    parser.add_argument("--client", required=True)
    parser.add_argument("--device-json", required=True)
    args = parser.parse_args()
    client = str(pathlib.Path(args.client).resolve())
    device_json = str(pathlib.Path(args.device_json).resolve())
    runner = str(pathlib.Path(__file__).with_name("radio_e2e_test.py"))
    prefix = re.compile(r"^\d{4}-\d{2}-\d{2}:\d{2}:\d{2}:\d{2} [!*+\-#&@i<>] \[[^\]]+\] ")
    for mode in range(4):
        with tempfile.TemporaryDirectory(prefix="scanner-log-test-") as directory:
            result = subprocess.run(
                [sys.executable, runner, "--client", client, "--device-json", device_json,
                 "--debug", str(mode)], cwd=directory, capture_output=True, text=True, timeout=120,
            )
            require(result.returncode == 0, result.stdout + result.stderr)
            require(not result.stderr, "Unexpected stderr: " + result.stderr)
            output = result.stdout.splitlines()
            application = [line for line in output if not line.startswith("E2E PASS:") and line]
            require(application and "Scanner node starting" in application[0], "Missing startup line")
            require(all(prefix.match(line) for line in application), "Unformatted console output")
            files = list(pathlib.Path(directory).glob("scanner_*.*"))
            if mode != 1:
                require(len(application) == 1, "Console must contain only startup")
            if mode < 2:
                require(not files, "Console/silent mode created a file")
            else:
                require(len(files) == 1, "Expected exactly one timestamped log")
                require(files[0].suffix == (".html" if mode == 3 else ".txt"), "Wrong log extension")
                text = files[0].read_text()
                require("[radio_frontend.c]" in text and "[scanner_session.c]" in text
                        and "[radio_commands.c]" in text and "[iq_recording.c]" in text
                        and "[commutator.c]" in text,
                        "Primary module events missing")
                require("opcode=0x02 status=0" in text and "hex=" in text, "UDP response metadata missing")
                packets = [line for line in text.splitlines() if "opcode=0x70 status=0" in line]
                require(packets and all("[payload omitted]" in line for line in packets), "IQ omission missing")
                for line in packets:
                    require(len(line.split("hex=", 1)[1].split("[payload omitted]", 1)[0].split()) == 10,
                            "IQ bytes leaked into packet hex")
                if mode == 3:
                    require(all(line.endswith("<br>") for line in text.splitlines()), "Missing HTML breaks")
                    require(" &lt; [udp_socket.c]" in text and " &gt; [udp_socket.c]" in text,
                            "HTML direction markers are not escaped")
                else:
                    require(all(prefix.match(line) for line in text.splitlines()), "Unformatted TXT output")
            failure = subprocess.run(
                [client, "--debug", str(mode), "--device-json", "missing-device.json"],
                cwd=directory, capture_output=True, text=True, timeout=15,
            )
            require(failure.returncode != 0 and "! [main.c] Device JSON:" in failure.stderr,
                    "Fatal failure was not printed")
            require(prefix.match(failure.stderr), "Fatal error has no timestamp/module")
        print("Logging mode {} PASS: destinations, formatting, packets and fatal output".format(mode))
    invalid = subprocess.run([client, "--debug", "4"], capture_output=True, text=True, timeout=15)
    require(invalid.returncode == 2, "Invalid debug mode accepted")


if __name__ == "__main__":
    main()