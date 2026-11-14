"""Privileged firmware publisher and unprivileged, boot-bound Rainmeter reader."""
import argparse
import datetime as dt
import json
from pathlib import Path
from boot_status import snapshot

CACHE = Path(r'D:\AI\bench\AD103-rainmeter-status.json')


def display_state(record, current_boot, now):
    # Missing evidence is never a closed padlock. Same kernel boot + freshness
    # are both required, including after a rapid restart or resumed session.
    if not current_boot or record.get('windows_boot_filetime') != current_boot:
        return 0
    age = (now - dt.datetime.fromisoformat(record['observed_utc'])).total_seconds()
    if not 0 <= age <= 150:
        return 0
    if record.get('live_gpu_state') == 'LOCKED':
        return 1
    if record.get('handoff_state') == 'UNLOCK_VERIFIED_AT_EFI_HANDOFF':
        return 2
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--publish', action='store_true')
    args = parser.parse_args()
    observation = snapshot()
    if args.publish:
        tmp = CACHE.with_suffix('.tmp')
        tmp.write_text(json.dumps(observation, indent=2) + '\n', encoding='utf-8')
        tmp.replace(CACHE)
        # A one-shot refresh runs through this existing elevated task so it
        # can reach Rainmeter even when Rainmeter itself was started elevated.
        refresh = CACHE.parent / 'AD103-rainmeter-refresh.request'
        if refresh.exists():
            refresh.unlink()
            import subprocess
            subprocess.run(['pwsh.exe', '-NoProfile', '-WindowStyle', 'Hidden', '-File', r'D:\AI\bench\rainmeter-unlock-widget\Finalize-WidgetTask.ps1'], timeout=20, check=False)
        return
    try:
        record = json.loads(CACHE.read_text(encoding='utf-8'))
        value = display_state(record, observation.get('windows_boot_filetime'), dt.datetime.now(dt.timezone.utc))
    except (OSError, ValueError, KeyError, TypeError):
        value = 0
    print(value)


if __name__ == '__main__':
    main()
