#!/usr/bin/env bash
# TezzNative Disaster Recovery Restore Script
# To recover TezzNative from SSH:
# 1. tar -xzf tezznative_source_backup.tar.gz -C /path/to/destination/
# 2. cd /path/to/destination/TezzNative
# 3. All source code, standard library, website, and tools are restored!
echo "Restoring TezzNative source code..."
tar -xzf tezznative_source_backup.tar.gz
echo "Restore complete!"
