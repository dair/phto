# Imager User Documentation

Welcome to the Imager user documentation. This directory contains comprehensive guides for building, configuring, and using the Imager library and its companion tools.

## Contents

| Document | Description |
|----------|-------------|
| [Getting Started](getting-started.md) | Install dependencies, build the project, write your first integration |
| [Configuration Guide](configuration.md) | TOML configuration format, multi-root setup, and best practices |
| [Library API Reference](api-reference.md) | Complete reference for the `imager::Imager` C++ API |
| [imagestore CLI](imagestore-cli.md) | Batch import tool: options, usage patterns, and pipeline integration |
| [imager_cli Demo](imager-cli.md) | Interactive command-line demo for exploring the library |
| [Supported Formats](formats.md) | Format-by-format validation details, limitations, and tips |
| [Storage and Data Model](storage.md) | How files are stored on disk, the database schema, and AAE sidecar handling |
| [Metrics and Monitoring](metrics.md) | Built-in lock-free instrumentation, available metrics, and snapshot format |
| [Architecture Overview](architecture.md) | Internal component map, concurrency model, and data flow |
| [Troubleshooting](troubleshooting.md) | Common errors, diagnosis steps, and workarounds |

## Quick Navigation

**I want to ingest a photo collection** — read [Getting Started](getting-started.md), then [imagestore CLI](imagestore-cli.md).

**I am embedding the library in my own application** — read [Getting Started](getting-started.md), then [Library API Reference](api-reference.md), then [Configuration Guide](configuration.md).

**I need to understand a specific error** — go to [Troubleshooting](troubleshooting.md).

**I want to understand how storage and deduplication work** — read [Storage and Data Model](storage.md).

**I want to observe pipeline performance** — read [Metrics and Monitoring](metrics.md).
