using Orkestr.Commands.Entities;

namespace Orkestr.Commands.Interfaces;

/// <summary>
///	Reads an argument vector and returns process options or an exit code.
///	Does not open a socket, create a logger, or start the engine.
///	A test double can return a finished result without reproducing flag syntax.
/// </summary>
public interface ICommandProvider
{
    /// <summary>
    ///	Walks the argument vector from left to right. Help and version are printed here,
    ///	because no logger exists yet. A rejected command is not an exception.
    /// </summary>
    /// <param name="arguments">Argument vector. Null is rejected. An empty vector selects defaults.</param>
    /// <returns>Options and exit code 0, or no options with exit code 0 or 2.</returns>
    /// <exception cref="ArgumentNullException">The argument vector is null.</exception>
    CommandResult Parse(IReadOnlyList<string> arguments);
}
