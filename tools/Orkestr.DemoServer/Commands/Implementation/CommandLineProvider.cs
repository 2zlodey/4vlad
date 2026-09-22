using System.CommandLine;
using System.CommandLine.Help;
using System.CommandLine.Invocation;
using System.CommandLine.Parsing;
using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using Orkestr.Commands.Interfaces;
using CommandResult = Orkestr.Commands.Entities.CommandResult;
using Orkestr.Core.Entities;
using Orkestr.Logging.Entities;

namespace Orkestr.Commands.Implementation;

/// <summary>
///	Reads the process argument vector and builds engine options, help, or an error.
///	Owns the System.CommandLine command tree: one root, the help and version actions, and the working options.
///	Does not open a socket, create a logger, or reference the engine. Help, version, and
///	errors are written here because the logger does not exist yet.
/// </summary>
public sealed class CommandLineProvider : ICommandProvider
{
    #region Fields

    private const int ErrorExitCode = 2;
    private const string ProductName = "Orkestr";
    private const string CommandDescription =
        "Listen for device handshake packets and answer with a 52-byte session.";

    private static readonly ParserConfiguration ParserConfiguration = new()
    {
        EnablePosixBundling = false,
        ResponseFileTokenReplacer = null
    };

    private static readonly Option<IPAddress> ListenAddressOption = new("--listen-address", "-a")
    {
        Description = "IPv4 address to bind.",
        HelpName = "ip",
        DefaultValueFactory = _ => EngineOptions.DefaultListenAddress,
        CustomParser = ParseListenAddress
    };

    private static readonly Option<int> ListenPortOption = new("--listen-port", "-p")
    {
        Description = "UDP port to bind, 1-65535.",
        HelpName = "port",
        DefaultValueFactory = _ => EngineOptions.DefaultListenPort,
        CustomParser = result => ParsePort(result, "listen port")
    };

    private static readonly Option<int> LogLevelOption = new("--log-level", "-l")
    {
        Description = "Minimum log level as a number.",
        HelpName = "n",
        DefaultValueFactory = _ => (int)EngineOptions.DefaultMinimumLogLevel,
        CustomParser = ParseLogLevel
    };

    private static readonly Option<string?> LogFileOption = new("--log-file", "-f")
    {
        Description = "Append logs to this file as well as the console. Default: console only",
        HelpName = "path",
        CustomParser = ParseLogFile
    };

    private static readonly Command Root = CreateRoot();

    private static bool ClearParseErrorsOnInvoke;

    #endregion

    /// <summary>
    ///	Reads the argument vector with System.CommandLine. An exact help token wins over every other token,
    ///	then an exact version token. Otherwise the vector is parsed and the first rejected token or error stops it.
    ///	Does not throw for a user mistake: it writes two lines to standard error and returns exit code 2.
    ///	A successful parse does not invoke the root command, so the engine is not created here.
    /// </summary>
    /// <param name="arguments">Argument vector already split by the host. Null is rejected.</param>
    /// <returns>Options with exit code 0, or no options with exit code 0 or 2.</returns>
    /// <exception cref="ArgumentNullException">The argument vector is null.</exception>
    public CommandResult Parse(IReadOnlyList<string> arguments)
    {
        ArgumentNullException.ThrowIfNull(arguments);

        if (ContainsExact(arguments, "--help") || ContainsExact(arguments, "-h"))
        {
            InvokeSymbol(["--help"]);
            return new CommandResult(null, 0);
        }

        if (ContainsExact(arguments, "--version"))
        {
            InvokeSymbol(["--version"]);
            return new CommandResult(null, 0);
        }

        ClearParseErrorsOnInvoke = false;
        ParseResult parsed = Root.Parse(arguments, ParserConfiguration);
        var rejected = RejectedToken(parsed);
        if (rejected is not null)
            return Fail($"Unexpected argument: {rejected}");

        if (parsed.Errors.Count > 0)
            return Fail(parsed.Errors[0].Message);

        var options = new EngineOptions(
            parsed.GetValue(ListenAddressOption)!,
            parsed.GetValue(ListenPortOption),
            (LogLevel)parsed.GetValue(LogLevelOption),
            parsed.GetValue(LogFileOption));
        return new CommandResult(options, 0);
    }

    #region Private methods

    /// <summary>
    ///	Parses one help or version token and runs only that action.
    ///	Parse errors are cleared for this call so the action is not reported as a failed parse.
    ///	The full argument vector is parsed with clearing off, otherwise an attached value such as
    ///	--help=text would lose its syntax error.
    /// </summary>
    /// <param name="arguments">A vector of one symbol token.</param>
    private static void InvokeSymbol(string[] arguments)
    {
        ClearParseErrorsOnInvoke = true;
        Root.Parse(arguments, ParserConfiguration).Invoke();
    }

    /// <summary>
    ///	Builds the command once. Help and version are explicit options. The root has no action,
    ///	so a later parse of working flags does not start the engine.
    /// </summary>
    /// <returns>The command whose options <see cref="Parse"/> reads.</returns>
    private static Command CreateRoot()
    {
        HelpOption help = new("--help", "-h")
        {
            Description = "Show this text and exit"
        };
        help.Action = new HelpTextAction((HelpAction)help.Action!);

        VersionOption version = new()
        {
            Description = "Show the assembly version and exit"
        };
        version.Action = new VersionTextAction();

        Command root = new(ProductName, CommandDescription);
        root.Options.Add(help);
        root.Options.Add(version);
        root.Options.Add(ListenAddressOption);
        root.Options.Add(ListenPortOption);
        root.Options.Add(LogLevelOption);
        root.Options.Add(LogFileOption);
        return root;
    }

    /// <summary>
    ///	Accepts an IPv4 address. Host names, the word any, and every IPv6 form are rejected.
    ///	DNS is not called.
    /// </summary>
    /// <param name="result">The single token supplied for the listen address.</param>
    /// <returns>The parsed address. <see cref="IPAddress.None"/> when the token is rejected and is not read.</returns>
    private static IPAddress ParseListenAddress(ArgumentResult result)
    {
        var value = TokenText(result);
        if (IPAddress.TryParse(value, out var address) &&
            address.AddressFamily == AddressFamily.InterNetwork)
        {
            return address;
        }

        result.AddError($"Invalid listen address: {value}. Expected an IPv4 address.");
        return IPAddress.None;
    }

    /// <summary>
    ///	Accepts an integer from 1 to 65535 with no sign, decimal point, or thousands separator.
    /// </summary>
    /// <param name="result">The single token supplied for the port.</param>
    /// <param name="name">English name used in the error, such as listen port.</param>
    /// <returns>The parsed port. Zero when the token is rejected and is not read.</returns>
    private static int ParsePort(ArgumentResult result, string name)
    {
        var value = TokenText(result);
        if (int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var port) &&
            port is >= 1 and <= 65535)
        {
            return port;
        }

        result.AddError($"Invalid {name}: {value}. Expected an integer from 1 to 65535.");
        return 0;
    }

    /// <summary>
    ///	Accepts one of the six digits that match <see cref="LogLevel"/>. Names and padded digits are rejected.
    /// </summary>
    /// <param name="result">The single token supplied for the log level.</param>
    /// <returns>The digit. Zero when the token is rejected and is not read.</returns>
    private static int ParseLogLevel(ArgumentResult result)
    {
        var value = TokenText(result);
        if (value is "0" or "1" or "2" or "3" or "4" or "5")
            return int.Parse(value, CultureInfo.InvariantCulture);

        result.AddError($"Invalid log level: {value}. Expected an integer 0, 1, 2, 3, 4, or 5.");
        return 0;
    }

    /// <summary>
    ///	Accepts a path that is not empty after trimming. The path is not opened.
    /// </summary>
    /// <param name="result">The single token supplied for the log file.</param>
    /// <returns>The trimmed path. Null when the token is rejected and is not read.</returns>
    private static string? ParseLogFile(ArgumentResult result)
    {
        var value = TokenText(result);
        if (value.Length > 0)
            return value;

        result.AddError($"Invalid log file path: {value}. Expected a non-empty path.");
        return null;
    }

    /// <summary>
    ///	Returns the first supplied token with surrounding whitespace removed.
    ///	The parser calls this only after a value token is present.
    /// </summary>
    /// <param name="result">Argument result for one option.</param>
    /// <returns>The trimmed token text.</returns>
    private static string TokenText(ArgumentResult result)
        => result.Tokens[0].Value.Trim();

    /// <summary>
    ///	Finds a directive or a bare double dash. The parser skips both without treating them as values.
    /// </summary>
    /// <param name="parsed">Result of parsing the original argument vector.</param>
    /// <returns>The token text, or null when neither kind is present. A bare double dash is the text --.</returns>
    private static string? RejectedToken(ParseResult parsed)
    {
        foreach (var token in parsed.Tokens)
        {
            if (token.Type is TokenType.Directive or TokenType.DoubleDash)
                return token.Value;
        }

        return null;
    }

    /// <summary>
    ///	Reports whether any element equals the token. This is not a substring search.
    /// </summary>
    /// <param name="arguments">Argument vector.</param>
    /// <param name="token">Exact element, including dashes.</param>
    /// <returns>True when one element is equal to the token.</returns>
    private static bool ContainsExact(IReadOnlyList<string> arguments, string token)
    {
        foreach (var argument in arguments)
        {
            if (argument == token)
                return true;
        }

        return false;
    }

    /// <summary>
    ///	Joins the <see cref="LogLevel"/> names in numeric order. There is no separate table of names.
    /// </summary>
    /// <returns>The line appended under the help text.</returns>
    private static string LogLevelLine()
    {
        var levels = string.Join(
            ", ",
            Enum.GetValues<LogLevel>().Select(level => $"{(int)level} {level}"));
        return $"Log levels: {levels}";
    }

    /// <summary>
    ///	Writes the error and the follow-up line, then returns exit code 2 with no options.
    /// </summary>
    /// <param name="message">First line, naming the bad token or value.</param>
    /// <returns>A result that does not start the engine.</returns>
    private static CommandResult Fail(string message)
    {
        Console.Error.WriteLine(message);
        Console.Error.WriteLine("Run with --help.");
        return new CommandResult(null, ErrorExitCode);
    }

    #endregion

    #region Actions

    /// <summary>
    ///	Prints the library help and then the log level line.
    ///	Owns the stock help action and does not parse working flags.
    ///	The engine is not started from this action.
    /// </summary>
    private sealed class HelpTextAction : SynchronousCommandLineAction
    {
        private readonly HelpAction _help;

        /// <summary>
        ///	Stores the stock help action that writes the description, usage, and options.
        /// </summary>
        /// <param name="help">Help action created by the help option before this action replaces it.</param>
        public HelpTextAction(HelpAction help)
        {
            _help = help;
        }

        /// <summary>
        ///	Tells the parser that help is not a failed parse.
        ///	Without this flag the library would keep the help request as an error.
        /// </summary>
        public override bool ClearsParseErrors => ClearParseErrorsOnInvoke;

        /// <summary>
        ///	Writes help to the invocation output, then a blank line and the log level names.
        ///	Trailing blank lines from the stock help are collapsed so the level line follows one blank line.
        /// </summary>
        /// <param name="parseResult">Parse of the single help token. Its output writer receives the text.</param>
        /// <returns>The exit code of the stock help action, which is zero.</returns>
        public override int Invoke(ParseResult parseResult)
        {
            var output = parseResult.InvocationConfiguration.Output;
            var buffer = new StringWriter();
            parseResult.InvocationConfiguration.Output = buffer;
            try
            {
                var code = _help.Invoke(parseResult);
                output.WriteLine(buffer.ToString().TrimEnd('\r', '\n'));
                output.WriteLine();
                output.WriteLine(LogLevelLine());
                return code;
            }
            finally
            {
                parseResult.InvocationConfiguration.Output = output;
            }
        }
    }

    /// <summary>
    ///	Prints one version line for this assembly.
    ///	Does not use the informational version attribute. A missing version prints the product name alone.
    ///	The engine is not started from this action.
    /// </summary>
    private sealed class VersionTextAction : SynchronousCommandLineAction
    {
        /// <summary>
        ///	Tells the parser that version is not a failed parse.
        ///	Without this flag the library would keep the version request as an error.
        /// </summary>
        public override bool ClearsParseErrors => ClearParseErrorsOnInvoke;

        /// <summary>
        ///	Writes <c>Orkestr</c> and the assembly version to the invocation output.
        /// </summary>
        /// <param name="parseResult">Parse of the single version token. Its output writer receives the line.</param>
        /// <returns>Zero. The process exits before the engine starts.</returns>
        public override int Invoke(ParseResult parseResult)
        {
            var version = Assembly.GetExecutingAssembly().GetName().Version;
            var line = version is null ? ProductName : $"{ProductName} {version}";
            parseResult.InvocationConfiguration.Output.WriteLine(line);
            return 0;
        }
    }

    #endregion
}
