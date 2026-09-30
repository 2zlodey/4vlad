using System.Reflection;
using System.Text;

namespace Orkestr.Codec.Entities;

/// <summary>
///	The application version the server expects in a VER response.
///	The bytes are the executing assembly version, ASCII, padded with zeros to ten bytes.
///	The server does not switch firmware when the response differs.
/// </summary>
public static class SoftwareVersion
{
    /// <summary>
    ///	Ten ASCII bytes of <see cref="AssemblyName.Version"/> for this assembly.
    ///	Callers receive a copy from <see cref="Current"/>.
    /// </summary>
    private static readonly byte[] _currentValueBuffer = ReadCurrent();

    /// <summary>
    ///	A copy of the ten version bytes. Changing the returned array does not change the stored value.
    /// </summary>
    public static byte[] Current => (byte[])_currentValueBuffer.Clone();

    #region Public methods

    /// <summary>
    ///	Compares a response field with this assembly version, including the trailing zeros.
    /// </summary>
    /// <param name="version">Ten bytes from a VER response.</param>
    /// <returns>True when every byte matches <see cref="Current"/>.</returns>
    public static bool Matches(ReadOnlySpan<byte> version)
    {
        return version.SequenceEqual(_currentValueBuffer);
    }

    /// <summary>
    ///	ASCII text of a version field without trailing zeros and spaces. The log line uses this text.
    /// </summary>
    /// <param name="version">Version bytes. A short span is still decoded.</param>
    /// <returns>The trimmed ASCII text. An all-zero field returns an empty string.</returns>
    public static string Format(ReadOnlySpan<byte> version)
    {
        return Encoding.ASCII.GetString(version).TrimEnd('\0', ' ');
    }

    #endregion

    /// <summary>
    ///	Reads <see cref="AssemblyName.Version"/> of the executing assembly into the ten-byte field.
    ///	A missing version becomes ten zeros. Text longer than the field is cut.
    /// </summary>
    /// <returns>The version bytes compared by VER.</returns>
    private static byte[] ReadCurrent()
    {
        var version = Assembly.GetExecutingAssembly().GetName().Version;
        var text = version is null ? string.Empty : version.ToString();
        var encoded = Encoding.ASCII.GetBytes(text);
        var buffer = new byte[VerLayout.VersionSize];
        encoded.AsSpan(0, Math.Min(encoded.Length, buffer.Length)).CopyTo(buffer);
        return buffer;
    }
}
