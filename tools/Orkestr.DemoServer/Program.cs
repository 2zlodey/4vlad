using System.Runtime.InteropServices;
using Orkestr.Commands.Implementation;
using Orkestr.Core.Implementation;

namespace Orkestr;

/// <summary>
///	Process entry point. Turns launch arguments into engine options, then runs until the
///	operating system delivers SIGINT or SIGTERM.
///	Owns no socket, registry, or packet parser. Those are created inside the engine after
///	the command provider returns options.
///	Argument text, help, and the exit code for a bad command stay in the command provider.
/// </summary>
public static class Program
{
    /// <summary>
    ///	Parses arguments and, when they describe a process, runs the engine until cancellation.
    ///	Registers POSIX signal handlers only after a successful parse, so a bad command
    ///	never opens a socket. An exception from the engine constructor or the run loop is
    ///	not caught here: the runtime exits with a non-zero status.
    /// </summary>
    /// <param name="args">Argument vector already split by the shell or by systemd.</param>
    /// <returns>Zero after help, version, or an orderly stop. Two when the arguments are rejected.</returns>
    public static async Task<int> Main(string[] args)
    {
        var parsed = new CommandLineProvider().Parse(args);
        if (parsed.Options is null)
            return parsed.ExitCode;

        using var cts = new CancellationTokenSource();
        using var term = PosixSignalRegistration.Create(
            PosixSignal.SIGTERM, _ => cts.Cancel());
        using var sigint = PosixSignalRegistration.Create(
            PosixSignal.SIGINT, _ => cts.Cancel());

        var engine = new OrkestrEngine(parsed.Options);
        await engine.RunAsync(cts.Token);
        return 0;
    }
}
