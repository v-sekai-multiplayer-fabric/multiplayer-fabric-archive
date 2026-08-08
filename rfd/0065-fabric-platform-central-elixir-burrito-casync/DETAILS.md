## The context

The multiplayer-fabric stack ships three Windows packages via MSIX: a
game client, a dedicated server, and a platform manager that installs
and updates the other two (and itself). The platform manager began as
a Godot placeholder with a single stub button and no real logic.

MSIX requires a native `.exe` as its `Executable=` entry point. The
earlier approach used a .NET 8 self-contained launcher stub (~60 MB)
to wrap the Godot dedicated-server export. The `zone-backend` Elixir
application already depends on `aria_storage`, a library that
implements the full casync content-addressable-sync protocol with
parallel chunk fetching, local caching, and integrity verification. A
second casync implementation for Godot (`multiplayer_fabric_asset`)
was registered as a Godot module but contained only a Python
build-config stub — no C++ source at all.

## The problem statement

1. The platform manager needs a single native `.exe` with no external
   runtime on the user's machine to satisfy the MSIX `Executable=`
   constraint.
2. A Godot export carries a full rendering and physics engine (~60–80
   MB) for a process whose only job is download management —
   unnecessary weight and a larger attack surface.
3. The casync implementation in `aria_storage` (Elixir) is complete,
   tested, and already depended on. Writing a second one in GDScript
   or C++ duplicates the protocol work and creates two things to
   maintain.
4. The .NET 8 self-contained launcher pattern introduced a .NET
   dependency and was approximately the same size as Godot with no
   benefit for the platform manager role.

## Design

**Language and runtime**: Replace the Godot project with an Elixir OTP
application (`fabric_platform_central`). Declare `aria_storage` as a
dependency so the updater delegates all casync work to the existing
implementation.

**Single executable**: Use [Burrito](https://github.com/burrito-elixir/burrito).
Burrito wraps a `mix release` into a self-extracting Zig-wrapper
binary that bundles ERTS + all BEAM code. The output is
`burrito_out/fabric_platform_central.exe` — one file, no separate
`.bat`, no shim, no sidecar.

**MSIX layout** (simplified by Burrito):

```
bin\
  fabric-platform-central.exe   ← Burrito single binary (MSIX Executable=)
assets\
  Square150x150Logo.png
  Square44x44Logo.png
  StoreLogo.png
AppxManifest.xml
```

**Update logic** in `lib/fabric_platform_central/updater.ex`:

```elixir
def update(target, install_dir, opts \\ []) when target in [:client, :server, :self] do
  {repo_base, index_file} = @targets[target]
  with {:ok, tag} <- latest_tag(repo_base, opts),
       index_url = "#{repo_base}/releases/download/#{tag}/#{index_file}",
       store_url = "#{repo_base}/releases/download/#{tag}/store" do
    AriaStorage.CasyncDecoder.decode_uri(index_url,
      store_uri: store_url,
      output_dir: Path.join(install_dir, to_string(target)),
      verify_integrity: true
    )
  end
end
```

**CI checks** (`.github/workflows/ci.yml`) — runs on every push and
PR:

```yaml
- run: mix compile --warnings-as-errors
- run: mix format --check-formatted
- run: mix test
```

**Release workflow** (`.github/workflows/release-msix.yml`) — fires on
`v*` tag push:

1. `erlef/setup-beam` — OTP 27 + Elixir 1.18
2. `goto-bus-stop/setup-zig@v2.2.1` — Zig 0.14.0 (required by Burrito)
3. `mix release` → `burrito_out/fabric_platform_central.exe`
4. `packaging/msix/pack.ps1 -BurritoExe ... -Version ...` → signed
   MSIX
5. Attach to GitHub Release

**`mix.exs` release config**:

```elixir
defp releases do
  [
    fabric_platform_central: [
      steps: [:assemble, &Burrito.wrap/1],
      burrito: [targets: [windows: [os: :windows, cpu: :x86_64]]]
    ]
  ]
end
```

## The downsides

- Burrito requires Zig installed in the release CI runner. Zig version
  compatibility with Burrito's Zig wrapper must be tracked as both
  projects release.
- ERTS bundled by Burrito adds ~30–50 MB to the MSIX; comparable to a
  Godot export but without the ability to strip it further without
  recompiling OTP.
- The OTP application starts in embedded mode via Burrito; some OTP
  introspection tools behave differently in embedded vs. interactive
  mode.

## The road not taken

- Godot + GDExtension casync: would require writing the casync
  protocol from scratch in C++, duplicating `aria_storage` with no
  gain. The `multiplayer_fabric_asset` module stub confirmed this was
  not started.
- Godot + GDScript HTTPRequest: could call `Add-AppxPackage` via
  `OS.execute()` for whole-file downloads but cannot do casync delta
  transfers without a full casync client in GDScript, and carries the
  rendering engine for no reason.
- Scoop shim + OTP release tree: avoids writing stub code, but still
  ships the full OTP release directory tree (hundreds of files) in the
  MSIX. Burrito produces a single file and stays Elixir-native.
- Go stub: a tiny `main.go` that execs the OTP `.bat` launcher works
  but adds a Go compile step and a non-Elixir source file, offering no
  advantage over Burrito.
- .NET 8 self-contained launcher: the original approach, removed
  because .NET 8 is a large dependency (~60 MB), the team does not
  otherwise use .NET, and it only wrapped a shell invocation.
