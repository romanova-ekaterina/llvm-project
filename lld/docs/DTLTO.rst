Distributed ThinLTO (DTLTO)
===========================

DTLTO enables the distribution of ThinLTO backend compilations via external 
distribution systems such as Incredibuild. Traditionally, ThinLTO compilations 
can be distributed by separating the thin-link, backend compilation, and link 
steps, allowing build systems that support dynamic dependencies (e.g., Bazel) 
to coordinate them. However, this often requires modifications to the user's 
build process.

DTLTO simplifies this by integrating distribution management directly into LLD 
as part of the standard link step. As a result, it can be used with any build 
process that supports in-process ThinLTO.

**Command-Line Interface**

The ``--thinlto-distribute`` option enables DTLTO and specifies the distribution
system to use:

.. code-block:: console

    --thinlto-distribute=<value>

where ``<value>`` indicates the distribution system kind.

Possible values: ``local`` (multithread on a local machine), ``sndbs``, 
``icecream``, ``distcc``, ``fastbuild``, ``goma``, ``make``, ``ninja``, 
``incredibuild``

Specifying this option causes DTLTO to distribute the ThinLTO compilation jobs.

**Example Usage**

To enable DTLTO using the local plugin as the distribution system:

.. code-block:: console

    --thinlto-distribute=local

**Important:** The compiler invoked must match the version of LLD to ensure 
compatibility.
