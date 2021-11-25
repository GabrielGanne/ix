project('ix', 'c',
        version : '0.1.0',
        default_options : [
            'warning_level=3',
            'werror=true',
            'c_std=c99',
        ],
        license : 'MIT',
        meson_version: '>= 0.49',
)


cc = meson.get_compiler('c')
configuration_inc = include_directories('src')

flags = [
    '-Wshadow',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wno-padded',
]
add_project_arguments(cc.get_supported_arguments(flags), language : 'c')

# write config file
config = configuration_data()
config.set('CONFIG_LOG2_CPU_CACHELINE_SIZE',get_option('lg2-cacheline-size'))
config.set('CONFIG_LOG2_CPU_PAGE_SIZE', get_option('lg2-page-size'))
configure_file(output : 'config.h', configuration : config)
add_project_arguments('-include', 'config.h', language : 'c')

libthread = dependency('threads')

#
# the simple hashtable library (sht)
#

# follow semantic versionning (https://semver.org)
sht_major = '0'  # incompatible API changes
sht_minor = '1'  # add backwards-compatible functionality
sht_patch = '0'  # backwards-compatible bug fixes
sht_version = sht_major + '.' + sht_minor + '.' + sht_patch

sht_sources = files(
        'src/common.h',
        'src/sht.c',
        'src/sht.h',
)
install_headers('src/sht.h')

sht = shared_library('sht',
        sht_sources,
        version : sht_version,
        install : true,
        include_directories : configuration_inc,
        dependencies : [libthread],
)

#
# TESTS
#

all_tests_sources = []
if get_option('tests')
    all_tests_sources += files(
        'test/sht-perf-test.c',
        'test/sht-smoketest.c',
        'test/sht-unittest.c',
    )

    # unit tests
    sht_unittest = executable('sht-unittest',
            files('test/sht-unittest.c'),
            include_directories : include_directories('src', 'test'),
            link_with : sht,
            dependencies : [libthread],
    )
    test('sht-unittest',
        sht_unittest,
        suite : 'unit-tests',
    )

    # smoke tests
    sht_smoketest = executable('sht-smoketest',
            files('test/sht-smoketest.c'),
            include_directories : include_directories('src', 'test'),
            link_with : sht,
            dependencies : [libthread],
    )
    test('sht-smoketest',
        sht_smoketest,
        suite : 'smoke-tests',
    )

    # pref test
    executable('sht-perf-test',
        files('test/sht-perf-test.c'),
        include_directories : include_directories('src', 'test'),
            link_with : sht,
            dependencies : [libthread],
    )
endif # tests

#
# DEVTOOLS
#

uncrustify = find_program('uncrustify', required : false)
if uncrustify.found()
    run_target('checkstyle',
        command : [
            uncrustify,
            '-l', 'c',
            '-c', join_paths(meson.source_root(), 'devtools', 'uncrustify.cfg'),
            '--check',
            sht_sources,
            all_tests_sources,
        ],
    )
    run_target('fixstyle',
        command : [
            uncrustify,
            '-l', 'c',
            '-c', join_paths(meson.source_root(), 'devtools', 'uncrustify.cfg'),
            '--replace',
            sht_sources,
            all_tests_sources,
        ],
    )
endif # uncrustify

codespell = find_program('codespell', required : false)
if codespell.found()
    run_target('spelling',
        command : [
            codespell,
            all_tests_sources,
        ]
    )
endif # codespell
