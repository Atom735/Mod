//! Current state of Zig migration:
//! * Existing MSVC workflows should be untouched
//! * Both Debug and Release{Fast,Safe} compile and start
//!   * Warnings are turned off
//!   * Plenty of miscompilations remain
//! * Custom UBSan runtime works great
//!   * Defaults to on
//!   * Execution of runtime-detectable Undefined Behavior will OutputDebugString
//!   * Currently spams logs too much, may want to disable with `-Dsanitize=false`
//! * Rewritten DLL export verifier script
//!   * Operates on actual DLL exports and (dumped list of) EXE imports
//!   * Lists exports that are not imported by the EXE
//! * Compiler repo is fetched directly by Zig
//! * Git hash version is parsed from `.git` directory
//!   * Git is no longer required to build
//! * Generate `compile_commands.json` for IDE integration
//!
//! What's next:
//! * Miscompilations
//!   * Inlining of uninlinable functions
//!     * Assumption that parameters are adjacent on the stack (e.g. varargs)
//!   * Undefined Behavior
//!   * ... What else?
//! * Correctness
//!   * Work through UBSan hits
//!   * Remove `-Wno-everything` and work through every warning
//!   * VTable verifier?
//! * Investigate Zig cache unreliability
//!   * Occasional `FileNotFound` or `AccessDenied` errors
//!   * Unsure if this is due to sketchy compdb generation, ZLS, Windows/NTFS, or some combination
//!   * Temporary workaround is to delete `zig-cache` (See `.vscode/tasks.json`)
//! * Missing developer workflows
//!   * Release process
//!   * Visual Studio non-Code support?
//!   * ... What else?
//! * Use static list of source files (generated using `Sources` struct)
//!
//! How to use this:
//! * Download https://ziglang.org/builds/zig-windows-x86_64-0.12.0-dev.1604+caae40c21.zip
//! * Place the contents somewhere on your `PATH` (e.g. `C:\Zig`)
//! * Run `zig build` to build with default settings (Debug build, FAssert on, UBSan on)
//! * Run `zig build --help` to see a list of various build flags, some ours, some Zig's
//! * Run `zig build -Doptimize=ReleaseFast` to build with optimizations and LTO
//!   * Note that this "ReleaseFast" is _not_ a "release" build; FAssert and UBSan will be enabled
//! * VSCode
//!   * Generating `compile_commands.json` makes VSCode a smarter IDE than the real deal Visual Studio
//!   * Extensions:
//!     * C/C++ - language & debugger support
//!     * clangd - fancy-pants IDE smarts AKA "IntelliSense"
//!     * Zig Language - fancy-pants IDE smarts AKA "IntelliSense"

const std = @import("std");

const Options = struct {
    optimize: std.builtin.OptimizeMode,
    target: std.zig.CrossTarget,

    release: ?[]const u8,
    fassert: bool,
    hardcoded_xml: bool,
    sanitize: bool,
    profiling: bool,
    emit_compdb: bool,
};

pub fn build(b: *std.Build) !void {
    const arena = b.allocator;

    // TODO: Is there a better way to manage installation of artifacts?
    b.install_prefix = ".";
    b.install_path = ".";

    const deps = b.dependency("compiler", .{});
    const opts = parseOptions(b);
    const sources = try Sources.init(b);
    const autogenerated = try codeGen(b, opts, sources);

    const dll = b.addSharedLibrary(.{
        .name = "CvGameCoreDLL",
        .target = opts.target,
        .optimize = opts.optimize,
        .use_llvm = true,
        .use_lld = true,
    });
    configure(dll, opts);
    addWindows(dll, deps);
    addMsvc(dll, deps);
    addBoost(dll, deps);
    addPython(dll, deps);
    addTbb(dll);

    const combine_compdb = b.addRunArtifact(b.addExecutable(.{
        .name = "combine_compile_commands",
        .root_source_file = .{ .path = "./Project Files/bin/combine_compile_commands.zig" },
        .optimize = .ReleaseSafe,
    }));
    const compdb = combine_compdb.captureStdOut();

    if (opts.emit_compdb) {
        var dir = try b.cache_root.handle.makeOpenPathIterable("tmp_compdb", .{});
        defer dir.close();

        // TODO: Fix intermittent tmp_compdb AccessDenied and FileNotFound errors.
        // The following doesn't work well when combining compdb's, but it makes generation robust.
        // I think a proper custom `std.Build.Step` with manual cache integration will be necessary.

        // var rng = std.rand.DefaultPrng.init(@bitCast(@as(i64, @truncate(std.time.nanoTimestamp()))));
        // const n = rng.random().int(usize);
        // var iter = dir.iterateAssumeFirstIteration();
        // while (try iter.next()) |file| {
        //     // Deleting files on Windows is asynchronous, so instead we rename them to something unique enough before deleting.
        //     // However, we need to make sure that we won't just iterate into the newly renamed file before its deletion goes through.
        //     // We use the "~" prefix to place the file before our current position in iteration.
        //     if (file.name[0] != '~') {
        //         const old = b.fmt("~{s}.{x}", .{ file.name, n });
        //         try dir.dir.rename(file.name, old);
        //         try dir.dir.deleteFile(old);
        //     }
        // }
    }

    for (sources.dll_cpp.keys()) |cpp_path| {
        const obj = b.addObject(.{
            .name = std.fs.path.basename(cpp_path),
            .target = opts.target,
            .optimize = opts.optimize,
            .use_llvm = true,
            .use_lld = true,
        });
        dll.addObject(obj);
        obj.addIncludePath(autogenerated);
        obj.addIncludePath(.{ .path = Sources.dll_path });
        configure(obj, opts);
        addWindows(obj, deps);
        addMsvc(obj, deps);
        addBoost(obj, deps);
        addPython(obj, deps);
        addTbb(obj);

        var flags = std.ArrayListUnmanaged([]const u8).initCapacity(arena, cpp_flags.len + ubsan_flags.len + 1) catch @panic("OOM");
        flags.appendSliceAssumeCapacity(cpp_flags);

        if (opts.sanitize) {
            flags.appendSliceAssumeCapacity(ubsan_flags);
        }

        if (opts.emit_compdb) {
            // We need a static output path that's unique but consistent for each source file to properly cache it.
            // This is not thread-safe, but it certainly beats busting the compiler cache with a unique path.
            const compdb_name = b.fmt("{s}.json", .{obj.name});
            const tmp_compdb_path = b.cache_root.join(arena, &.{ "tmp_compdb", compdb_name }) catch @panic("OOM");

            const tmp_compdb = arena.create(std.Build.GeneratedFile) catch @panic("OOM");
            tmp_compdb.* = .{ .step = &obj.step, .path = tmp_compdb_path };

            const cached_compdb = b.addWriteFiles().addCopyFile(.{ .generated = tmp_compdb }, compdb_name);
            combine_compdb.addFileArg(cached_compdb);

            flags.appendAssumeCapacity(b.fmt("-MJ{s}", .{tmp_compdb_path}));
        }

        obj.addCSourceFile(.{ .file = .{ .path = cpp_path }, .flags = flags.toOwnedSlice(arena) catch @panic("OOM") });
    }

    // Clang is generating calls to the new C++ exception handler, but MSVC++ 2003 does not provide the new one.
    // Instead, we're just creating a shim handler that calls MSVC++ 2003's.
    const shim = b.addObject(.{
        .name = "shim",
        .root_source_file = .{ .path = "./Project Files/DLLSources/shim.zig" },
        .target = opts.target,
        .optimize = .ReleaseFast,
    });
    dll.addObject(shim);

    const ubsan_runtime = b.addObject(.{
        .name = "ubsan_runtime",
        .root_source_file = .{ .path = "./Project Files/DLLSources/ubsan_runtime.zig" },
        .target = opts.target,
        .optimize = .ReleaseFast,
    });
    dll.addObject(ubsan_runtime);

    const dll_export = b.addExecutable(.{
        .name = "dll_export",
        .root_source_file = .{ .path = "./Project Files/bin/dll_export.zig" },
        .optimize = .ReleaseSafe,
    });

    const dump_exe_imports = b.addRunArtifact(dll_export);
    dump_exe_imports.addArg("dump_exe");
    dump_exe_imports.addFileArg(.{ .path = if (b.args) |args| args[0] else "../../Colonization_PitBoss.exe" });
    const vanilla_imports = dump_exe_imports.addOutputFileArg("vanilla_imports.txt");

    const verify_dll_exports = b.addRunArtifact(dll_export);
    verify_dll_exports.addArg("verify_dll");
    verify_dll_exports.addFileArg(.{ .path = "./Project Files/bin/vanilla_imports.txt" });
    verify_dll_exports.addArtifactArg(dll);

    // The way that Zig Build integrates with LLD doesn't allow us to enable LAA,
    // so we have our own build script to manually flip that bit in the PE header.
    const enable_laa = b.addRunArtifact(b.addExecutable(.{
        .name = "enable_laa",
        .root_source_file = .{ .path = "./Project Files/bin/enable_laa.zig" },
        .optimize = .ReleaseSafe,
    }));
    enable_laa.addArtifactArg(dll);
    const laa_dll = enable_laa.addOutputFileArg("CvGameCoreDLL.dll");

    b.getInstallStep().dependOn(&b.addInstallFile(laa_dll, "./Assets/CvGameCoreDLL.dll").step);
    b.getInstallStep().dependOn(&b.addInstallFile(dll.getEmittedPdb(), "./Assets/CvGameCoreDLL.pdb").step);
    if (opts.emit_compdb) {
        b.getInstallStep().dependOn(&b.addInstallFile(compdb, "compile_commands.json").step);
    }

    const dump_exe = b.addWriteFiles();
    dump_exe.addCopyFileToSource(vanilla_imports, "./Project Files/bin/vanilla_imports.txt");
    b.step("dump_exe", "Dump DLL imports from given EXE file into vanilla_imports.txt").dependOn(&dump_exe.step);
}

fn parseOptions(b: *std.Build) Options {
    const optimize = b.standardOptimizeOption(.{});
    const release = b.option([]const u8, "release", "Set release version and enable release-ready defaults (default: `git rev-parse HEAD`)");
    return .{
        .optimize = optimize,
        .target = .{
            .cpu_arch = .x86,
            // `x86_64_v3` _should_ be general enough to support every user,
            // but we may want to consider downgrading to `x86_64_v2` if issues arise.
            // See <https://en.wikipedia.org/wiki/X86-64#Microarchitecture_levels>
            .cpu_model = if (release != null) .{ .explicit = &std.Target.x86.cpu.x86_64_v3 } else .native,
            .os_tag = .windows,
            .abi = .none,
        },
        .release = release,
        .fassert = b.option(bool, "fassert", "Enable FAssert (default: true)") orelse (release == null),
        .hardcoded_xml = b.option(bool, "hardcoded_xml", "Hardcode XML values into the DLL (default: false)") orelse false,
        .sanitize = b.option(bool, "sanitize", "Enable UBSan (default: true)") orelse (release == null),
        .profiling = b.option(bool, "profiling", "Enable profiling (default: false)") orelse false,
        .emit_compdb = b.option(bool, "compdb", "Emit compile_commands.json (default: false)") orelse false,
    };
}

fn codeGen(b: *std.Build, opts: Options, sources: *const Sources) !std.Build.LazyPath {
    const autogenerated = b.addWriteFiles();
    const perl = b.findProgram(&.{"perl"}, &.{}) catch @panic("Could not find perl on PATH!");
    const xml_deps = std.mem.concat(b.allocator, []const u8, &.{ sources.xml.keys(), &.{"./Project Files/bin/XMLlists.pm"} }) catch @panic("OOM");

    // TODO: Rewrite `xml_validation.pl` script in Zig.
    const xml_validation = b.addSystemCommand(&.{ perl, "./bin/xml_validation.pl" });
    xml_validation.setName("xml_validation.pl");
    xml_validation.cwd = .{ .path = "./Project Files" };
    xml_validation.extra_file_dependencies = std.mem.concat(b.allocator, []const u8, &.{ sources.dll_cpp.keys(), xml_deps }) catch @panic("OOM");
    autogenerated.step.dependOn(&xml_validation.step);

    // TODO: Rewrite `CPP_line_checker.pl` script in Zig.
    const cpp_line_checker = b.addSystemCommand(&.{ perl, "./bin/CPP_line_checker.pl" });
    cpp_line_checker.setName("CPP_line_checker.pl");
    cpp_line_checker.cwd = .{ .path = "./Project Files" };
    cpp_line_checker.extra_file_dependencies = std.mem.concat(b.allocator, []const u8, &.{ sources.dll_cpp.keys(), sources.dll_h.keys() }) catch @panic("OOM");
    autogenerated.step.dependOn(&cpp_line_checker.step);

    // TODO: Rewrite `xml_enum_global_types.pl` script in Zig.
    const xml_enum_global_types = b.addSystemCommand(&.{ perl, "./bin/xml_enum_global_types.pl" });
    xml_enum_global_types.setName("xml_enum_global_types.pl");
    xml_enum_global_types.cwd = .{ .path = "./Project Files" };
    xml_enum_global_types.extra_file_dependencies = xml_deps;
    _ = autogenerated.addCopyFile(xml_enum_global_types.addOutputFileArg("AutoGlobalDefineEnum.h"), "./autogenerated/AutoGlobalDefineEnum.h");
    _ = autogenerated.addCopyFile(xml_enum_global_types.addOutputFileArg("AutoGlobalDefineEnumCpp.h"), "./autogenerated/AutoGlobalDefineEnumCpp.h");
    _ = autogenerated.addCopyFile(xml_enum_global_types.addOutputFileArg("AutoGlobalDefineEnumCase.h"), "./autogenerated/AutoGlobalDefineEnumCase.h");

    // TODO: Rewrite `xml_enum_gen.pl` script in Zig.
    const xml_enum_gen = b.addSystemCommand(&.{ perl, "./bin/xml_enum_gen.pl" });
    xml_enum_gen.setName("xml_enum_gen.pl");
    xml_enum_gen.cwd = .{ .path = "./Project Files" };
    xml_enum_gen.extra_file_dependencies = xml_deps;
    _ = autogenerated.addCopyFile(xml_enum_gen.addOutputFileArg("AutoXmlEnum.h"), "./autogenerated/AutoXmlEnum.h");
    _ = autogenerated.addCopyFile(xml_enum_gen.addOutputFileArg("AutoXmlTest.h"), "./autogenerated/AutoXmlTest.h");
    _ = autogenerated.addCopyFile(xml_enum_gen.addOutputFileArg("AutoXmlDeclare.h"), "./autogenerated/AutoXmlDeclare.h");
    _ = autogenerated.addCopyFile(xml_enum_gen.addOutputFileArg("AutoXmlInit.h"), "./autogenerated/AutoXmlInit.h");
    _ = autogenerated.addCopyFile(xml_enum_gen.addOutputFileArg("AutoXmlPreload.h"), "./autogenerated/AutoXmlPreload.h");

    // TODO: Rewrite `variable_setup.pl` script in Zig.
    const variable_setup = b.addSystemCommand(&.{ perl, "./bin/variable_setup.pl" });
    variable_setup.setName("variable_setup.pl");
    variable_setup.cwd = .{ .path = "./Project Files" };
    variable_setup.extra_file_dependencies = xml_deps;
    _ = autogenerated.addCopyFile(variable_setup.addOutputFileArg("AutoVariableFunctions.h"), "./autogenerated/AutoVariableFunctions.h");
    _ = autogenerated.addCopyFile(variable_setup.addOutputFileArg("AutoVariableFunctionsCPP.h"), "./autogenerated/AutoVariableFunctionsCPP.h");
    _ = autogenerated.addCopyFile(variable_setup.addOutputFileArg("AutoVariableFunctionsCase.h"), "./autogenerated/AutoVariableFunctionsCase.h");

    // TODO: Rewrite `InfoArray.pl` script in Zig.
    const info_array = b.addSystemCommand(&.{ perl, "./bin/InfoArray.pl" });
    info_array.setName("InfoArray.pl");
    info_array.cwd = .{ .path = "./Project Files" };
    info_array.extra_file_dependencies = xml_deps;
    _ = autogenerated.addCopyFile(info_array.addOutputFileArg("AutoInfoArray.h"), "./autogenerated/AutoInfoArray.h");

    _ = autogenerated.add("./autogenerated/AutoGitVersion.h", b.fmt(
        \\#ifndef AUTO_GIT_VERSION_H
        \\#define AUTO_GIT_VERSION_H
        \\#pragma once
        \\char const* szGitVersion = "{[version]s}";
        \\#endif
    , .{
        .version = opts.release orelse blk: {
            var git = Git.init(b) catch break :blk null;
            defer git.deinit();
            const head = git.head() catch break :blk null;
            const real_head = git.realRef(head) catch break :blk null;
            break :blk b.allocator.dupe(u8, real_head.hash) catch @panic("OOM");
        } orelse "UNKNOWN",
    }));

    return autogenerated.getDirectory();
}

/// See <https://clang.llvm.org/docs/ClangCommandLineReference.html>
/// See <https://clang.llvm.org/docs/DiagnosticsReference.html>
const cpp_flags = &[_][]const u8{
    "-std=c++2b",

    // Troubleshooting
    // TODO: List out every warning we currently trip.
    "-Wno-everything",
    "-ferror-limit=99999999",

    // "-Weverything",
    "-Werror",

    // Irrelevant warnings
    "-Wno-c++11-extensions",
    "-Wno-c++98-compat",

    // TODO: Re-enable these warnings once both the codebase and the Microsoft include headers have been cleaned up to be spec compliant.
    "-Wno-microsoft-extra-qualification",
    "-Wno-microsoft-template-shadow",
    "-Wno-microsoft-template",
    "-Wno-microsoft-enum-forward-reference",

    // TODO: Disable these workarounds once both the cosebase and the Microsoft include headers have been cleaned up to be spec compliant.
    // See <https://clang.llvm.org/docs/MSVCCompatibility.html>
    "-fms-extensions",
    "-fms-compatibility",
    "-fms-compatibility-version=13.10.6030",
    "-fdelayed-template-parsing",
    "-fno-short-enums",

    // MSVC++ 2003 did not support `__wchar_t` yet, so all `wchar_t`s got typedef'd to `unsigned short`.
    "-Xclang",
    "-fno-wchar",

    // TODO: Does this even do anything for CodeView?
    "-fdebug-macro",

    // TODO: Fix EnumMap code
    "-Wno-deprecated-anon-enum-enum-conversion",

    // TODO: Re-enable these warnings once the codebase isn't shit.
    "-Wno-c++11-compat-deprecated-writable-strings",
    "-Wno-switch",
    "-Wno-non-pod-varargs",
    "-Wno-unused-parameter",
    "-Wno-deprecated-copy-with-user-provided-copy",

    // Boost: Unknown compiler version
    "-Wno-#pragma-messages",

    // Modifying export names via `#pragma comment(linker, "/EXPORT:...")`
    "-Wno-comment",

    // Python
    "-Wno-deprecated-register",

    // For some reason, Zig is passing an unused argument to Clang.
    // Whenever we change these flags, we should try disabling this to verify that they're being used as expected.
    // See: <https://github.com/ziglang/zig/blob/b677b3627818edc24828f36f8269a3c3843703a1/src/Compilation.zig#L4248>
    "-Wno-unused-command-line-argument",

    // MSVC++ 2003 did not have thread-safe initialization of statics.
    "-fno-threadsafe-statics",

    // Original Makefile uses MSVC's `/EHsc` which disables catching SEH exceptions but enables C++ exceptions.
    // See <https://learn.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model>
    "-fno-async-exceptions",
    "-fcxx-exceptions",
    "-fexceptions", // TODO: Redundant?
};

/// See <https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html>
const ubsan_flags = &[_][]const u8{
    "-fsanitize=undefined",
    "-fsanitize-recover=all",
};

fn configure(obj: *std.Build.Step.Compile, opts: Options) void {
    // TODO: Do we care about `linker_dynamicbase`?
    obj.linker_dynamicbase = true;

    // TODO: Do we need `dll_export_fns`?
    obj.dll_export_fns = true;

    // TODO: Does this break anything?
    obj.link_gc_sections = true;
    obj.link_function_sections = true;
    obj.link_data_sections = true;

    // This flag controls Zig adding UBSan flags for us, but we want to use `ubsan_flags` instead.
    obj.disable_sanitize_c = true;

    // TODO: Measure performance gain from `omit_frame_pointer`.
    obj.omit_frame_pointer = false;

    // TODO: Figure out how we want to propagate debug mode to C++ without misconfiguring library include headers.
    if (obj.optimize == .Debug) {
        obj.defineCMacro("_DEBUG", null);
    } else {
        obj.defineCMacro("NDEBUG", null);
    }

    if (opts.hardcoded_xml) {
        obj.defineCMacro("HARDCODE_XML_VALUES", null);
    }

    if (opts.release != null) {
        obj.defineCMacro("FINAL_RELEASE", null);
        obj.want_lto = obj.kind != .obj;
    } else {
        obj.defineCMacro("FASSERT_ENABLE", null);
    }

    // TODO: Replace profiling with Tracy.
    if (opts.profiling) {
        obj.defineCMacro("PROFILING_ENABLED", null);
    }
}

// TODO: Figure out which macro defines are provided by Zig, which are provided by Clang, and which we need to provide.
fn addWindows(obj: *std.Build.Step.Compile, dep: *std.Build.Dependency) void {
    switch (obj.kind) {
        .obj => {
            obj.defineCMacro("NOMINMAX", null);
            obj.defineCMacro("WIN32_LEAN_AND_MEAN", null);
            obj.defineCMacro("WIN32", null);
            obj.defineCMacro("_WINDOWS", null);
            obj.defineCMacro("_DLL", null);
            obj.defineCMacro("_MT", null);
            obj.addSystemIncludePath(dep.path("./Microsoft SDKs/Windows/v6.0/Include"));
        },
        .lib => {
            obj.addLibraryPath(dep.path("./Microsoft SDKs/Windows/v6.0/Lib"));
            obj.linkSystemLibrary("kernel32");
            obj.linkSystemLibrary("winmm");
            obj.linkSystemLibrary("user32");
            obj.linkSystemLibrary("shell32");
        },
        else => unreachable,
    }
}

fn addMsvc(obj: *std.Build.Step.Compile, dep: *std.Build.Dependency) void {
    switch (obj.kind) {
        .obj => {
            obj.addSystemIncludePath(dep.path("./Microsoft Visual C++ Toolkit 2003/include"));
        },
        .lib => {
            obj.addLibraryPath(dep.path("./Microsoft Visual C++ Toolkit 2003/lib"));
            obj.linkSystemLibrary("msvcrt");
            obj.linkSystemLibrary("msvcprt");
            // TODO: Can `oldnames` this be replaced by `/ALTERNATENAME`?
            obj.linkSystemLibrary("oldnames");
        },
        else => unreachable,
    }
}

fn addBoost(obj: *std.Build.Step.Compile, dep: *std.Build.Dependency) void {
    switch (obj.kind) {
        .obj => {
            // TODO: Do we need/want these? What's the best way to include Boost in this setup?
            // obj.defineCMacro("BOOST_USE_WINDOWS_H", null);
            // obj.defineCMacro("__CORRECT_ISO_CPP_STRING_H_PROTO", null);
            obj.addIncludePath(dep.path("lib/Boost-1.32.0/include"));
        },
        .lib => {
            obj.addLibraryPath(dep.path("./lib/Boost-1.32.0/libs"));
            obj.linkSystemLibrary("boost_python-vc71-mt-1_32");
        },
        else => unreachable,
    }
}

fn addPython(obj: *std.Build.Step.Compile, dep: *std.Build.Dependency) void {
    switch (obj.kind) {
        .obj => {
            obj.defineCMacro("uintptr_t", "DWORD_PTR"); // `pyport.h` wants to use `uintptr_t`
            obj.addIncludePath(dep.path("lib/Python24/include"));
        },
        .lib => {
            obj.addLibraryPath(dep.path("./lib/Python24/libs"));
            obj.linkSystemLibrary("python24");
        },
        else => unreachable,
    }
}

// TODO: Move TBB out of source tree.
// TODO: Experiment with `__INTEL_COMPILER` to convince TBB to not treat us like we're retarded (MSVC).
fn addTbb(obj: *std.Build.Step.Compile) void {
    switch (obj.kind) {
        .obj => {
            // Without this, TBB will try linking itself via `#pragma comment(lib, ...)`
            obj.defineCMacro("__TBBMALLOC_NO_IMPLICIT_LINKAGE", null);
            obj.addIncludePath(.{ .path = "./Project Files/DLLSources/tbb" });
        },
        .lib => {
            obj.addLibraryPath(.{ .path = "./Project Files/tbb" });
            obj.linkSystemLibrary("tbb");
            obj.linkSystemLibrary("tbbmalloc");
        },
        else => unreachable,
    }
}

// TODO: Write list of files to separate file and only iterate directories when generating new `sources.zig`.
//       Once Zig supports `@import`ing ZON files, we should instead output to ZON so that `build.zig.zon` can correctly list all files.
//       See <https://github.com/ziglang/zig/issues/14531>
const Sources = struct {
    arena: std.mem.Allocator,

    dll_dir: std.fs.IterableDir,
    python_dir: std.fs.IterableDir,
    xml_dir: std.fs.IterableDir,

    dll_cpp: std.StringArrayHashMapUnmanaged(void) = .{},
    dll_h: std.StringArrayHashMapUnmanaged(void) = .{},
    py: std.StringArrayHashMapUnmanaged(void) = .{},
    xml: std.StringArrayHashMapUnmanaged(void) = .{},

    const dll_path = "./Project Files/DLLSources";
    const python_path = "./Assets/Python";
    const xml_path = "./Assets/XML";

    const Kind = enum {
        cpp,
        h,
        py,
        xml,
        fn fromName(name: []const u8) ?Kind {
            inline for (@typeInfo(Kind).Enum.fields) |kind| {
                if (std.ascii.endsWithIgnoreCase(name, "." ++ kind.name)) {
                    return @enumFromInt(kind.value);
                }
            }
            return null;
        }
    };

    fn init(b: *std.Build) !*Sources {
        var dll_dir = try b.build_root.handle.openIterableDir(dll_path, .{});
        errdefer dll_dir.close();

        var python_dir = try b.build_root.handle.openIterableDir(python_path, .{});
        errdefer python_dir.close();

        var xml_dir = try b.build_root.handle.openIterableDir(xml_path, .{});
        errdefer xml_dir.close();

        const sources = b.allocator.create(Sources) catch @panic("OOM");
        sources.* = .{
            .arena = b.allocator,
            .dll_dir = dll_dir,
            .python_dir = python_dir,
            .xml_dir = xml_dir,
        };

        try sources.iterateDllSources();
        try sources.iteratePython();
        try sources.iterateXml();

        return sources;
    }

    fn close(sources: *Sources) void {
        sources.dll_dir.close();
        sources.python_dir.close();
        sources.xml_dir.close();
    }

    fn iterateDllSources(sources: *Sources) !void {
        sources.dll_cpp.clearAndFree(sources.arena);

        var dll_walker = try sources.dll_dir.walk(sources.arena);
        defer dll_walker.deinit();

        while (try dll_walker.next()) |dll_file| {
            if (dll_file.kind != .file) continue;
            const kind = Kind.fromName(dll_file.basename) orelse continue;
            switch (kind) {
                .cpp => {
                    const path = std.fs.path.join(sources.arena, &.{ dll_path, dll_file.path }) catch @panic("OOM");
                    sources.dll_cpp.putNoClobber(sources.arena, path, {}) catch @panic("OOM");
                },
                .h => {
                    const path = std.fs.path.join(sources.arena, &.{ dll_path, dll_file.path }) catch @panic("OOM");
                    sources.dll_h.putNoClobber(sources.arena, path, {}) catch @panic("OOM");
                },
                else => continue,
            }
        }
    }

    fn iteratePython(sources: *Sources) !void {
        sources.py.clearAndFree(sources.arena);

        var py_walker = try sources.python_dir.walk(sources.arena);
        defer py_walker.deinit();

        while (try py_walker.next()) |py_file| {
            if (py_file.kind != .file) continue;
            const kind = Kind.fromName(py_file.basename) orelse continue;
            if (kind != .py) continue;
            const path = std.fs.path.join(sources.arena, &.{ python_path, py_file.path }) catch @panic("OOM");
            sources.py.putNoClobber(sources.arena, path, {}) catch @panic("OOM");
        }
    }

    fn iterateXml(sources: *Sources) !void {
        sources.xml.clearAndFree(sources.arena);

        var xml_walker = try sources.xml_dir.walk(sources.arena);
        defer xml_walker.deinit();

        while (try xml_walker.next()) |xml_file| {
            if (xml_file.kind != .file) continue;
            const kind = Kind.fromName(xml_file.basename) orelse continue;
            if (kind != .xml) continue;
            const path = std.fs.path.join(sources.arena, &.{ xml_path, xml_file.path }) catch @panic("OOM");
            sources.xml.putNoClobber(sources.arena, path, {}) catch @panic("OOM");
        }
    }
};

const Git = struct {
    arena: std.mem.Allocator,
    dir: std.fs.Dir,

    fn init(b: *std.Build) !Git {
        var dir = try b.build_root.handle.openDir(".git", .{});
        errdefer dir.close();
        return .{ .arena = b.allocator, .dir = dir };
    }

    fn deinit(git: *Git) void {
        git.dir.close();
    }

    const Ref = union(enum) {
        ref: []const u8,
        hash: []const u8,
    };

    fn parseRef(git: *Git, string: []const u8) !Ref {
        const kind = if (std.mem.startsWith(u8, string, "ref: ")) Ref.ref else Ref.hash;
        const start: usize = if (kind == .ref) 5 else 0;
        const end = std.mem.indexOfScalar(u8, string, '\n') orelse return error.CorruptGit;
        const ref_or_hash = git.arena.dupe(u8, string[start..end]) catch @panic("OOM");
        return switch (kind) {
            .ref => .{ .ref = ref_or_hash },
            .hash => .{ .hash = ref_or_hash },
        };
    }

    fn readRef(git: *Git, ref: Ref) !Ref {
        std.debug.assert(ref == .ref);
        const ref_str = git.dir.readFileAlloc(git.arena, ref.ref, std.math.maxInt(usize)) catch return git.readPackedRef(ref);
        return try git.parseRef(ref_str);
    }

    fn readPackedRef(git: *Git, ref: Ref) !Ref {
        std.debug.assert(ref == .ref);
        const packed_refs = git.dir.readFileAlloc(git.arena, "packed-refs", std.math.maxInt(usize)) catch |err| switch (err) {
            error.OutOfMemory => @panic("OOM"),
            else => return err,
        };
        var line_iter = std.mem.tokenizeScalar(u8, packed_refs, '\n');
        while (line_iter.next()) |line| {
            if (line[0] == '#') continue;
            std.debug.assert(line.len > 42);
            std.debug.assert(line[40] == ' ');
            const hash = line[0..40];
            const ref_str = line[41..];
            if (std.mem.eql(u8, ref.ref, ref_str)) {
                return .{ .hash = hash };
            }
        }
        return error.BadRef;
    }

    fn realRef(git: *Git, ref: Ref) !Ref {
        var curr = ref;
        while (curr == .ref) {
            curr = try git.readRef(curr);
        }
        return curr;
    }

    fn head(git: *Git) !Ref {
        return try git.readRef(.{ .ref = "HEAD" });
    }
};
