# Foundry-Java Android export fixture

This fixture is the smallest descriptor-bearing Java module used by the
Foundry Android exporter integration tests. The tests compile
`DemoExtension.java` against the exact merged Foundry-Java runtime JAR, package
the descriptor and narrow ProGuard rules into a deterministic module JAR, and
then consume it through both explicit local files and a staged Maven repository.

The fixture deliberately contains no Android or Foundry host native library.
Foundry-Java owns descriptor and binding-payload validation; Foundry supplies
only the explicit artifact graph and requested ABI set.
