import os
from building import *

# get current directory
cwd = GetCurrentDir()

# The set of source files associated with this SConscript file.
src = Glob('common/*.c')
src += Glob('ioLibrary/*.c')
src += Glob('ioLibrary/wizchip/*.c')
src += Glob('ioLibrary/DHCP/*.c')
src += Glob('ioLibrary/DNS/*.c')
src += Glob('ioLibrary/W5500/*.c')
src += Glob('W5500Client/*.c')
src += Glob('platform/RT-Thread/*.c')

path = [cwd + '/common']
path += [cwd + '/ioLibrary']
path += [cwd + '/ioLibrary/wizchip']
path += [cwd + '/ioLibrary/DHCP']
path += [cwd + '/ioLibrary/DNS']
path += [cwd + '/ioLibrary/W5500']
path += [cwd + '/W5500Client']
path += [cwd + '/platform/RT-Thread']

if GetDepend(['PKG_USING_RYANW5500_EXAMPLE']):
    src += Glob('example/*.c')
    path += [cwd + '/example']

group = DefineGroup('RyanW5500', src, depend=[
                    "PKG_USING_RYANW5500"], CPPPATH=path)

Return('group')
