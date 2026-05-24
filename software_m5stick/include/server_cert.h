#pragma once

// Self-signed certificate from software_server/cert.pem (valid 10 years).
// Passed as cert_pem to esp_http_client so mbedTLS can verify the server
// without needing CONFIG_ESP_TLS_INSECURE or skip_cert_verify.
static const char SERVER_CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIC4jCCAcqgAwIBAgIUUpm+9drOF4JF5nmZ+0LiNfy4j4QwDQYJKoZIhvcNAQEL\n"
    "BQAwGDEWMBQGA1UEAwwNY2FjdHVzLXNlcnZlcjAeFw0yNjA1MjQxMTM3MDVaFw0z\n"
    "NjA1MjExMTM3MDVaMBgxFjAUBgNVBAMMDWNhY3R1cy1zZXJ2ZXIwggEiMA0GCSqG\n"
    "SIb3DQEBAQUAA4IBDwAwggEKAoIBAQCN/g8g9N4t1jtlt8QLA2G4sZaTrq8RPqIy\n"
    "dWzCr7gW8s2/6+sf4YsvsKseG9cPIB7lioKyQ4cpMBla56HHenX9hyUj+EtV8jfn\n"
    "X12vDJOMKNvl9t+WiO2cBx+Oz7Uv0BgCDYy2xrVCc+lKUCXNShY35NODKVVpxekm\n"
    "YbajhUnML7XnTmqmagqCoOHxrlWY7RLzT7byphjDdkRdp4/b+RIC8Gd4C258zP42\n"
    "JysczHBa9MT7Lfqm8RJRRub9BWFTYK0yEf9r/qR/5ki47CZ8xGGEexI57GDOlnvh\n"
    "k6ELOr+zaBa8p+NQ7Piu7UPkDA37wCY7GdNnRTMn79ew5WqoFDiXAgMBAAGjJDAi\n"
    "MCAGA1UdEQQZMBeCCWxvY2FsaG9zdIcEfwAAAYcEwKgBjDANBgkqhkiG9w0BAQsF\n"
    "AAOCAQEAY2WvhobxjRynPQnZBz+PZMK1KG7nBuRhY5OhTFX58l8gLbPogZHiJTCd\n"
    "9BwwE/Zlf0+9S9RxaVeugyya+kmZrlwf0I5jFDCPBcC8ddgLo97VIq6GglJeaRqI\n"
    "LaMjrI/Hy6cpxpmAmnQJNGGisddFCMTBIithJTN3yVYMXVmf3b/fe0odO7U9xTBR\n"
    "vSDBBvh9+bWUD6NnWEgD/HKqnmMzEoZ6N5rsvzgqAKzefKA1RTTXhM5SV81/0kNb\n"
    "FfjnMoA4JVYPTWeTKQbdJ9pNo8T/TKRwoTcmphD3iOXa0h6ghglp75YOItbz1wWX\n"
    "P8NUNUHXP+FIketTy1BZ+LitBH7ggw==\n"
    "-----END CERTIFICATE-----\n";
