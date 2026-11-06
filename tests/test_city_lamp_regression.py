#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Seven real city lamps: production native provenance, live bulbs and character shadows."""
import sys
from test_m2_lamp_self_shadow_gpu import main

if __name__ == '__main__':
    sys.argv.append('--city-regression')
    main()
