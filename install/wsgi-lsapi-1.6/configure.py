import sys
import os
from distutils.sysconfig import get_config_var

def do_replace(s, vars):
    for v in vars:
        s = s.replace('@@' + v + '@@', get_config_var(v))
    return s

def osx_framework_hack():
    fw_path = get_config_var('PYTHONFRAMEWORKINSTALLDIR')
    if not fw_path:
        return
    try:
        os.unlink('Python.framework')
    except:
        pass
    os.symlink(fw_path, 'Python.framework')

def main():
    inf = open('Makefile.in')
    s = inf.read()
    inf.close()

    s = do_replace(s, [
        'CC',
        'CFLAGS',
        'INCLUDEPY',
        'LIBPL',
        'LINKFORSHARED',
        'LIBS',
        'SYSLIBS',
        ])

    defines = ''
    if sys.platform.startswith('freebsd'):
        defines = '-DHAVE_FREEBSD_SENDFILE'
    s = s.replace('@@DEFINES@@', defines)

    # For compatibility with Mac OS X 10.5/Python 2.6
    ldflags = get_config_var('LDFLAGS')
    libdir = ' -L' + get_config_var('LIBDIR')
    ldflags = ldflags + libdir 
    if 'MacOSX10.4u.sdk' in ldflags: # Might need a better test in the future
        ldflags = '-mmacosx-version-min=10.4 ' + ldflags
    s = s.replace('@@LDFLAGS@@', ldflags)

    ver = get_config_var('VERSION')
    if sys.version_info < (3, 8):
        if sys.version_info > (3, 0):
            ver = ver + 'm'
    s = s.replace('@@VERSION@@', ver)
    
    outf = open('Makefile', 'w')
    outf.write(s)
    outf.close()

    osx_framework_hack()

    print("Done.")
    
if __name__ == '__main__':
    main()
