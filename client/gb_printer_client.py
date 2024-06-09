#!/usr/bin/env python3
import re
import requests
from argparse import ArgumentParser, Namespace
from apscheduler.schedulers.blocking import BlockingScheduler
from pathlib import Path


def get_image_path(output_directory: Path | str) -> Path:
    """
    Create path of next image.

    Parameters
    ----------
    output_directory : Path | str
        Path in which images will be stored.
    """
    def path_format(index: int):
        index_str = str(index).zfill(4)
        return output_directory / f'gb-image-{index_str}.png'

    # Find all images in this path.
    found_images = list(map(lambda x: str(x), output_directory.glob('gb-image-*.png')))
    if not found_images:
        return path_format(0)

    # Sort them and find next image.
    found_images.sort()
    next_index = int(re.search(r'\d{4}', found_images[-1]).group(0)) + 1

    return path_format(next_index)


def run_client(args: Namespace):
    """
    Run GB Printer client.

    Parameters
    ----------
    args : Namespace
        'argparse' parameters.
    """
    def task():
        # Get status.
        status_resp = requests.get(f'{args.address}/status')
        if status_resp.status_code != 200:
            return

        # Parse status data.
        status_data = {}
        for entry in status_resp.text.split('\n'):
            # Skip empty entry.
            if not entry:
                continue

            entry_kv = list(map(lambda x: x.strip(), entry.split('=')))
            assert len(entry_kv) == 2

            status_data[entry_kv[0]] = entry_kv[1]

        # Show current status.
        print(status_data)

        # Download and store image if ready.
        if status_data['image_ready'] == '1':
            image_resp = requests.get(f'{args.address}/image')
            if image_resp.status_code != 200:
                print(f'Failed to read the image, status code: {image_resp.status_code}')
                return

            image_path = get_image_path(args.output_directory)
            with open(image_path, mode='wb') as file:
                file.write(image_resp.content)

            # Remove image on target device after it is stored.
            del_image_resp = requests.delete(f'{args.address}/delete-image')
            if del_image_resp.status_code != 200:
                print(
                    f'Failed to remove the image from target device, status code: {del_image_resp.status_code}')

    scheduler = BlockingScheduler()
    scheduler.add_job(task, 'interval', seconds=args.poll_interval)

    try:
        scheduler.start()
    except (KeyboardInterrupt, SystemExit):
        pass


if __name__ == '__main__':
    parser = ArgumentParser('GB Printer emulator client')
    parser.add_argument('--address', default='http://gb-printer',
                        help='GB Printer device address. Default: %(default)s')
    parser.add_argument('--poll_interval', type=float, default=0.5,
                        help='Interval between status polls in seconds. Default: %(default)s')
    parser.add_argument('--output_directory', type=Path, default=Path(__file__).parent,
                        help='Path in which images will be stored. Default: %(default)s')
    args = parser.parse_args()

    run_client(args)
