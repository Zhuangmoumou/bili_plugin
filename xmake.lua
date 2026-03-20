add_rules('mode.release', 'mode.debug')

set_languages('cxx17', 'c11')
set_warnings('all')
set_exceptions('cxx')

target('bili_plugin')
    set_kind('shared')
    add_rules('qt.shared')

    add_files('src/*.cpp')
    add_files('src/*.h')

    add_frameworks(
        'QtCore',
        'QtQuick',
        'QtQml',
        'QtNetwork',
        'QtMultimedia',
        'QtGui'
    )
