renderer_service = executable(
    'dleyna-renderer-service',
    files('daemon.c'),
    dependencies : [
        glib,
        gio,
        dleyna_core,
        renderer_dep,
    ],
    install: true
)

configure_file(
    input: 'com.intel.dleyna-renderer.service.in',
    output: 'com.intel.dleyna-renderer.service',
    configuration: conf
)
