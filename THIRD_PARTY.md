# Optional general-graph dependency

The general-graph prototype uses SDSL from
https://github.com/simongog/sdsl-lite at commit
`c32874cb2d8524119f25f3b501526fe692df29f4`.

SDSL is licensed under **GPL-3.0-or-later**, as stated in its `COPYING` file and
source headers. It is not covered by SEGA's MIT license. SDSL-linked executables
must not be described as MIT-only. Source bundles include upstream notices.

`cmake/sdsl_core.cmake` builds the unmodified upstream core needed for wavelet
trees. It does not build upstream suffix-array code, fetch submodules, change
global optimization flags or disable our sanitizer instrumentation. The optional
dependency can be omitted with `-DSEGA_BUILD_GENERAL=OFF`.

The separate MEGAHIT dependency and its existing license notices remain in
`libs/megahit`.

The optional `sega_assembly` shared library also links this SDSL backend.
The Sarand application adapter in `../research/neighborhoods/` integrates with
Sarand commit `a7e7a8a22efc6e66a496708d01b15196f5cb2980` from
https://github.com/beiko-lab/sarand. Sarand's GPL-3.0 license is retained at
`../research/neighborhoods/upstream/sarand/LICENSE.txt`; this integrated research
application must not be described as MIT-only. The upstream source is unmodified.

The `sega_megahit_assembly` target links MEGAHIT's GPL-3.0-or-later SDBG code and
does not depend on SDSL. Its application integration also uses the pinned GPL
Sarand source above. Source and license notices for MEGAHIT remain in
`libs/megahit`.
# Representation measurement integrations

The optional benchmark setup downloads separate upstream projects at the commits
recorded in `representations/upstreams.json`. Their source trees retain their own
licenses: Bifrost (BSD 2-Clause), MetaGraph (GPL v3), and MEGAHIT (GPL v3).
The binary environment packages also retain their
upstream licenses and package metadata. The SEGA license does not replace those
licenses. The source-only benchmark bundle does not include their compiled
binaries or relicense their source. Setup records its compatibility patches and
exact dependency environment alongside the compiled adapters.
