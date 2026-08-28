#!/bin/bash
PLUGIN_DIR=/usr/lib/x86_64-linux-gnu/dde-grand-search-daemon/plugins/searcher

killall dde-grand-search-daemon 2>/dev/null
pkill -f ide-project-search-plugin 2>/dev/null

sudo rm -f "$PLUGIN_DIR/ide-project-search-plugin"
sudo rm -f "$PLUGIN_DIR/ide-project-search.conf"

killall dde-grand-search-daemon 2>/dev/null && dde-grand-search-daemon &
