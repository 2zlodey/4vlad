using Orkestr.Core.Entities;

namespace Orkestr.Commands.Entities;

/// <summary>
///	Outcome of reading the argument vector. Either the process options are ready, or the
///	process must exit without creating the engine.
///	Help and version use exit code 0 with no options. A rejected command uses exit code 2.
///	Options are present only together with exit code 0.
/// </summary>
public sealed class CommandResult
{
    /// <summary>
    ///	Pairs options with an exit code and rejects a result that would both start the engine and fail.
    /// </summary>
    /// <param name="options">Options for the engine, or null when the process must exit first.</param>
    /// <param name="exitCode">Zero when the engine may start or when help or version was printed. Two when the command was rejected.</param>
    /// <exception cref="ArgumentException">A result that starts the engine has exit code 0.</exception>
    public CommandResult(EngineOptions? options, int exitCode)
    {
        if (options is not null && exitCode != 0)
            throw new ArgumentException(
                "A result that starts the engine has exit code 0.");

        Options = options;
        ExitCode = exitCode;
    }

    #region Properties

    /// <summary>
    ///	Options for the engine constructor, or null when help, version, or a bad command ends the process.
    /// </summary>
    public EngineOptions? Options { get; }

    /// <summary>
    ///	Status returned from Main when <see cref="Options"/> is null. Zero or two.
    /// </summary>
    public int ExitCode { get; }

    #endregion
}
