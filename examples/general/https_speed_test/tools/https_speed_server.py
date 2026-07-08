#!/usr/bin/env python3
import argparse
import http.server
import os
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path
from urllib.parse import urlsplit


DEFAULT_CERT_DIR = '/tmp/simple_test_https_speed_server'
DEFAULT_FILE_NAME = 'download.bin'
DEFAULT_PORT = 8443
DEFAULT_SIZE_MB = 256
BLOCK_SIZE = 256 * 1024


def parse_args():
    parser = argparse.ArgumentParser(
        description='Run a local HTTP/HTTPS server for ESP download speed tests.'
    )
    parser.add_argument('--host', default='0.0.0.0', help='IP address to bind.')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help='TCP port to bind.')
    parser.add_argument(
        '--http',
        action='store_true',
        help='Run plain HTTP instead of HTTPS. Useful for comparing TLS overhead.',
    )
    parser.add_argument(
        '--size-mb',
        type=int,
        default=DEFAULT_SIZE_MB,
        help='Content-Length of the generated download file in MiB.',
    )
    parser.add_argument(
        '--advertise-ip',
        help='IP address printed in the ESP URL. Use the PC address on the shield-box router LAN.',
    )
    parser.add_argument('--cert', help='Existing PEM certificate to use.')
    parser.add_argument('--key', help='Existing PEM private key to use.')
    parser.add_argument(
        '--cert-dir',
        default=DEFAULT_CERT_DIR,
        help='Directory for the generated self-signed certificate.',
    )
    parser.add_argument(
        '--regen-cert',
        action='store_true',
        help='Regenerate the self-signed certificate before starting.',
    )
    args = parser.parse_args()

    if args.size_mb <= 0:
        parser.error('--size-mb must be greater than 0')
    if args.port <= 0 or args.port > 65535:
        parser.error('--port must be in 1..65535')
    if bool(args.cert) != bool(args.key):
        parser.error('--cert and --key must be provided together')
    if args.http and (args.cert or args.key):
        parser.error('--cert and --key are only used for HTTPS')
    return args


def get_local_ip():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(('8.8.8.8', 80))
            return sock.getsockname()[0]
    except OSError:
        pass

    try:
        infos = socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)
        for info in infos:
            ip = info[4][0]
            if not ip.startswith('127.'):
                return ip
    except OSError:
        pass

    return '127.0.0.1'


def ensure_cert(cert_dir, advertise_ip, regen):
    cert_dir = Path(cert_dir)
    cert_dir.mkdir(parents=True, exist_ok=True)
    cert_path = cert_dir / 'server_cert.pem'
    key_path = cert_dir / 'server_key.pem'
    conf_path = cert_dir / 'openssl.cnf'

    if regen:
        cert_path.unlink(missing_ok=True)
        key_path.unlink(missing_ok=True)

    if cert_path.exists() and key_path.exists():
        return cert_path, key_path

    san_entries = ['DNS:localhost', 'IP:127.0.0.1']
    try:
        socket.inet_aton(advertise_ip)
        if advertise_ip != '127.0.0.1':
            san_entries.append(f'IP:{advertise_ip}')
    except OSError:
        san_entries.append(f'DNS:{advertise_ip}')

    conf_path.write_text(
        '\n'.join(
            [
                '[req]',
                'distinguished_name = req_distinguished_name',
                'x509_extensions = v3_req',
                'prompt = no',
                '',
                '[req_distinguished_name]',
                'CN = wifi-storage-speed-test',
                '',
                '[v3_req]',
                f"subjectAltName = {', '.join(san_entries)}",
                'keyUsage = keyEncipherment, digitalSignature',
                'extendedKeyUsage = serverAuth',
                '',
            ]
        ),
        encoding='utf-8',
    )

    cmd = [
        'openssl',
        'req',
        '-x509',
        '-newkey',
        'rsa:2048',
        '-sha256',
        '-days',
        '3650',
        '-nodes',
        '-keyout',
        str(key_path),
        '-out',
        str(cert_path),
        '-config',
        str(conf_path),
    ]

    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    except FileNotFoundError:
        print('ERROR: openssl is not installed. Install openssl or pass --cert and --key.', file=sys.stderr)
        sys.exit(2)
    except subprocess.CalledProcessError as err:
        print('ERROR: failed to generate self-signed certificate.', file=sys.stderr)
        print(err.stderr.decode(errors='replace'), file=sys.stderr)
        sys.exit(2)

    return cert_path, key_path


def format_mib(num_bytes):
    return num_bytes / 1024 / 1024


class SpeedHandler(http.server.BaseHTTPRequestHandler):
    server_version = 'SimpleTestSpeed/1.0'
    protocol_version = 'HTTP/1.1'

    def do_HEAD(self):
        if self._is_download_path():
            self._send_download_headers()
            return
        self._send_index(head_only=True)

    def do_GET(self):
        if self._is_download_path():
            self._send_download()
            return
        self._send_index()

    def _is_download_path(self):
        return urlsplit(self.path).path == f'/{DEFAULT_FILE_NAME}'

    def _send_download_headers(self):
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(self.server.download_size))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Connection', 'close')
        self.end_headers()

    def _send_download(self):
        self._send_download_headers()
        remaining = self.server.download_size
        sent = 0
        started = time.monotonic()

        try:
            while remaining > 0:
                chunk = self.server.payload[: min(BLOCK_SIZE, remaining)]
                self.wfile.write(chunk)
                sent += len(chunk)
                remaining -= len(chunk)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
            pass
        finally:
            elapsed = max(time.monotonic() - started, 0.001)
            mib_s = format_mib(sent) / elapsed
            mbps = sent * 8 / elapsed / 1000 / 1000
            print(
                f'{self.client_address[0]} sent {format_mib(sent):.1f} MiB '
                f'in {elapsed:.2f}s, {mib_s:.2f} MiB/s, {mbps:.2f} Mbit/s'
            )

    def _send_index(self, head_only=False):
        url = self.server.advertised_url
        body = (
            f'Simple test {self.server.scheme.upper()} speed server\n'
            f'Download URL: {url}\n'
            f'Download size: {format_mib(self.server.download_size):.0f} MiB\n'
        ).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Connection', 'close')
        self.end_headers()
        if not head_only:
            self.wfile.write(body)

    def log_message(self, fmt, *args):
        print(f'{self.client_address[0]} {fmt % args}')


class ThreadingHTTPServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    args = parse_args()
    advertise_ip = args.advertise_ip or get_local_ip()
    download_size = args.size_mb * 1024 * 1024
    scheme = 'http' if args.http else 'https'
    cert_path = None
    key_path = None

    if not args.http:
        if args.cert and args.key:
            cert_path = Path(args.cert)
            key_path = Path(args.key)
        else:
            cert_path, key_path = ensure_cert(args.cert_dir, advertise_ip, args.regen_cert)

    advertised_url = f'{scheme}://{advertise_ip}:{args.port}/{DEFAULT_FILE_NAME}'
    httpd = ThreadingHTTPServer((args.host, args.port), SpeedHandler)
    httpd.download_size = download_size
    httpd.payload = os.urandom(BLOCK_SIZE)
    httpd.advertised_url = advertised_url
    httpd.scheme = scheme

    if not args.http:
        ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_context.minimum_version = ssl.TLSVersion.TLSv1_2
        ssl_context.load_cert_chain(certfile=cert_path, keyfile=key_path)
        httpd.socket = ssl_context.wrap_socket(httpd.socket, server_side=True)

    print(f'{scheme.upper()} speed server started')
    print(f'  Bind: {args.host}:{args.port}')
    print(f'  URL:  {advertised_url}')
    print(f'  Size: {args.size_mb} MiB')
    if cert_path is not None:
        print(f'  Cert: {cert_path}')
    print('')
    print('Set CONFIG_WIFI_STORAGE_DOWNLOAD_TEST_URL to this URL, or update')
    print('wifi_storage/main/Kconfig.projbuild default before building.')
    print('Use --advertise-ip if the printed IP is not the PC LAN IP.')
    print('Press Ctrl+C to stop.')

    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print('')
    finally:
        httpd.server_close()


if __name__ == '__main__':
    main()
