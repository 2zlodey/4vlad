namespace Orkestr.Logging.Implementation;

/// <summary>
///	Builds the source string that every log call places beside the message.
///	The string is the caller's namespace and class, without the assembly name.
///	Free labels such as "engine" or "transport" are not used.
/// </summary>
public static class LogSource
{
    /// <summary>
    ///	Reads the runtime type of the caller. A proxy or a test double reports its own type.
    /// </summary>
    /// <param name="instance">Object that is writing the line. Null is rejected.</param>
    /// <returns>Full type name, or the short name when the runtime has no full name.</returns>
    /// <exception cref="ArgumentNullException">The instance is null.</exception>
    public static string From(object instance)
    {
        ArgumentNullException.ThrowIfNull(instance);

        var type = instance.GetType();
        return type.FullName ?? type.Name;
    }
}
