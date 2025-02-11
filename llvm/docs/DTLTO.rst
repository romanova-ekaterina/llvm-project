===================
DTLTO
===================
.. contents::
   :local:
   :depth: 2

.. toctree::
   :maxdepth: 1

Distributed ThinLTO (DTLTO)
===========================

Distributed ThinLTO (DTLTO) facilitates the distribution of backend ThinLTO
compilations via external distribution systems such as Incredibuild.

The existing method of distributing ThinLTO compilations via separate thin-link,
backend compilation, and link steps often requires significant changes to the
user's build process to adopt, as it requires using a build system that can
handle the dynamic dependencies specified by the index files, such as Bazel.

DTLTO eliminates this need by managing distribution internally within the LLD
linker during the traditional link step. This allows DTLTO to be used with any
build process that supports in-process ThinLTO.

Supported Features
------------------

- Archives (regular and thin) are fully supported.
- The ThinLTO cache is currently not supported.
- Only ELF and COFF platforms are currently supported.
- Only a limited set of LTO configurations are currently supported, e.g., 
  support for basic block sections is not currently available.

Overview of Operation
---------------------

For each ThinLTO compilation, LLD generates:

1. The required ThinLTO summary index files.
2. The list of dependency files that will be used for cross-module importing.
3. A list of input and output files.
4. A command line for the Clang compiler to perform codegen.

This information is supplied to a plugin for a distribution system, which
prepares and submits compilation jobs to the distribution system. Upon
completion, LLD integrates the compiled native object files into the link
process. Temporary files, such as the summary index files, are cleaned up.

This design keeps the details of distribution systems out of the LLVM source
code.

Workflow
--------

The workflow for DTLTO is the following:

.. code-block:: console

    Linker -> DTLTO frontend -> "Distribution system"-specific plugin.

"Distribution system"-specific plugins
--------------------------------------

"Distribution system"-specific plugins (DLLs) are responsible for preparing and
submitting compilation jobs to the distribution system.

"Distribution system"-specific plugin errors are handled via the linker's
diagnostic handle. These plugins can be implemented in any programming language
that can be compiled into a binary. The interface for the plugins is a simple C
interface.

Example plugins can be found in `llvm/lib/DTLTO/plugins`.

How Plugins Are Invoked
-----------------------

LLD provides the ``--thinlto-distribute`` option to enable DTLTO and to choose
which distribution system to use. The linker will invoke the DTLTO frontend,
which will load a distribution system-specific plugin that will handle ThinLTO
compilation jobs.

- LLD documentation: https://lld.llvm.org/DTLTO.html

Plugin Interfaces
-----------------

The following interfaces are defined:

.. code-block:: cpp

    // Performs code generation on the distributed build system.
    int (*dtltoPerformCodegenTy)(const void *Cfg, size_t NodesNum,
                                 struct dtltoBitcodeNodeTy **BitcodeNodes,
                                 size_t Argc, const char **Argv);

Plugin interfaces are declared in `llvm/include/llvm/DTLTO/Plugin.h`.

Temporary Files
---------------

During its operation, DTLTO generates several temporary files. Temporary files
are created in the same directory as the linker's output file, and their
filenames include the linker's PID and task number for uniqueness:

- **Native Object Files**:
    - Format:  `<Module ID stem>.<Task>.<PID>.native.o`
    - Example: `my.1.77380.native.o` (for bitcode module `my.o`).

- **Summary Index Files**:
    - Format:  `<Module ID stem>.<Task>.<PID>.native.o.thinlto.bc`
    - Example: `my.1.77380.native.o.thinlto.bc` (for bitcode module `my.o`).

- **Import Files**:
    - Format:  `<Module ID stem>.<Task>.<PID>.native.o.imports`
    - Example: `my.1.77380.native.o.imports` (for bitcode module `my.o`).

Constraints
-----------

- Matching versions of Clang and LLD should be used.
