#!/usr/bin/env python3
"""Provisional evidence (D-13): run NHL94AGA disks in PUAE (libretro-uae, the WinUAE-derived core) set up
as a stock A1200, as accurately as it allows: model A1200 with 2 MB Chip RAM and no Fast or slow RAM,
68020 at 14.19 MHz (multiplier 4), cycle-exact CPU and chipset ("exact"), AGA, PAL, the owner's
Kickstart 39.106. Headless, through the repository's libretro frontend (tools/oracle/retro.py), as fast
as the host runs (the emulated machine's own time is what the programs measure).

The first disk gets a Startup-Sequence that runs COMMAND. Extra disks go in DF1:-DF3: (the ROM halves).
After the run the core is unloaded (disk writes flushed) and the programs' logs are read off the disk.
Kickstart images and ROM disks stay out of git.

Usage: puaerun.py CORE.so KICKSTART SECONDS "COMMAND" OUTDIR DISK.adf [DISK2.adf ...]"""
import ctypes as C
import os
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'oracle'))
import retro  # noqa: E402

ENV_GET_LOG_INTERFACE = 27
RETRO_PIXEL_FORMAT_XRGB8888 = 1
log_printf_t = C.CFUNCTYPE(None, C.c_int, C.c_char_p)   # (the core's varargs are ignored: the format is logged)
LOG = []


def _log(level, fmt):
    if level >= 2 and fmt:                                 # warnings and errors
        LOG.append(fmt.decode('latin-1', 'replace').rstrip())


class PuaeCore(retro.Core):
    """the repository's frontend, with the log interface and the 32-bit pixel format PUAE asks for"""
    def _environment(self, cmd, data):
        if cmd == ENV_GET_LOG_INTERFACE:
            self._cb_log = log_printf_t(_log)
            C.cast(data, C.POINTER(C.c_void_p))[0] = C.cast(self._cb_log, C.c_void_p).value
            return True
        if cmd == retro.ENV_SET_PIXEL_FORMAT and C.cast(data, C.POINTER(C.c_int))[0] == RETRO_PIXEL_FORMAT_XRGB8888:
            self.pixel_format = RETRO_PIXEL_FORMAT_XRGB8888
            return True
        return super()._environment(cmd, data)

    def screenshot(self, path):
        if not self.frame or self.pixel_format != RETRO_PIXEL_FORMAT_XRGB8888:
            return super().screenshot(path)
        buf, w, h, pitch = self.frame
        rows = []
        for y in range(h):
            line = buf[y * pitch: y * pitch + w * 4]
            row = bytearray([0])
            for i in range(0, w * 4, 4):
                row += bytes((line[i + 2], line[i + 1], line[i]))
            rows.append(bytes(row))
        with open(path, 'wb') as f:
            f.write(retro._png(w, h, b''.join(rows)))
        return True

OPTIONS = {
    'puae_model': 'A1200OG',
    'puae_kickstart': 'kick39106.A1200',
    'puae_cpu_model': '68020',
    'puae_cpu_compatibility': 'exact',
    'puae_cpu_multiplier': '4',
    'puae_cpu_throttle': '0.0',
    'puae_chipmem_size': '4',
    'puae_bogomem_size': '0',
    'puae_fastmem_size': '0',
    'puae_z3mem_size': '0',
    'puae_fpu_model': '0',
    'puae_video_standard': 'PAL',
    'puae_floppy_speed': '100',
    'puae_floppy_write_protection': 'disabled',
    'puae_floppy_write_redirect': 'disabled',
    'puae_floppy_multidrive': 'enabled',
    'puae_use_whdload': 'disabled',
    'puae_use_boot_hd': 'disabled',
    'puae_immediate_blits': 'false',
}


def main(core_so, kick, seconds, command, out, disks):
    if os.environ.get('PUAE_FASTMEM'):                    # (a measurement off the stock machine: Fast RAM in MB)
        OPTIONS['puae_fastmem_size'] = os.environ['PUAE_FASTMEM']
    os.makedirs(out, exist_ok=True)
    system = os.path.join(out, 'system')
    os.makedirs(system, exist_ok=True)
    shutil.copy(kick, os.path.join(system, 'kick39106.A1200'))
    run_disks = []
    for i, d in enumerate(disks):
        dst = os.path.join(out, 'df%d.adf' % i)
        shutil.copy(d, dst)
        run_disks.append(dst)
    startup = os.path.join(out, 'startup')
    with open(startup, 'w') as f:
        f.write('Echo "puaerun: %s"\n%s\nEcho "puaerun: done"\n' % (command, command))
    subprocess.run(['xdftool', run_disks[0], 'delete', 's/Startup-Sequence', '+', 'write', startup, 's/Startup-Sequence'],
                   check=True, stdout=subprocess.DEVNULL)
    m3u = os.path.join(out, 'run (MD).m3u')
    with open(m3u, 'w') as f:
        f.write('\n'.join(run_disks) + '\n')
    core = PuaeCore(core_so, OPTIONS, sysdir=system)
    av = core.load(m3u)
    fps = av.timing.fps or 50.0
    total = int(seconds * fps)
    times = [float(t) for t in os.environ['PUAE_SHOTS'].split(',')] if os.environ.get('PUAE_SHOTS') else (10, 20, 30, 60, 120, 300, 600, 1200, 2400)
    shots = {int(t * fps) for t in times if t < seconds}   # (PUAE_SHOTS=t1,t2,...: screenshots at those emulated seconds)
    print('puaerun: %d frames at %.3f Hz, %dx%d' % (total, fps, av.geometry.base_width, av.geometry.base_height), flush=True)
    for n in range(1, total + 1):
        core.run(1)
        if n in shots:
            core.screenshot(os.path.join(out, 'shot-%07.1fs.png' % (n / fps)))
        if n % int(fps * 300) == 0:
            print('puaerun: %d s emulated' % int(n / fps), flush=True)
    core.screenshot(os.path.join(out, 'shot-end.png'))
    if LOG:
        print('core warnings: ' + ' | '.join(sorted(set(LOG))[:20]))
    core.lib.retro_unload_game()
    core.lib.retro_deinit()
    for name in ('NHL94AGA.log', 'NHL94VFY.log'):
        dst = os.path.join(out, name)
        r = subprocess.run(['xdftool', run_disks[0], 'read', name, dst], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode == 0 and os.path.exists(dst):
            print('== %s' % name)
            print(open(dst, encoding='latin-1').read())


if __name__ == '__main__':
    if len(sys.argv) < 7:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6:])
