using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Windows.Media;

namespace VisionFlow.Studio.Execution;

/// <summary>
/// 日志级别（与显示颜色一一对应）。
/// </summary>
public enum LogLevel
{
    Debug,    // 灰     #9CA3AF
    Info,     // 深灰   #18181B
    Warn,     // 黄     #CA8A04
    Error,    // 红     #DC2626
    Success,  // 绿     #16A34A
}

/// <summary>
/// 单条日志记录（不可变）。
/// </summary>
public sealed class LogEntry
{
    public DateTime Time { get; init; } = DateTime.Now;
    public LogLevel Level { get; init; }
    public string Source { get; init; } = "";   // 来源：节点名/模块名
    public string Message { get; init; } = "";

    public string LevelText => Level switch
    {
        LogLevel.Debug   => "DBG",
        LogLevel.Info    => "INF",
        LogLevel.Warn    => "WRN",
        LogLevel.Error   => "ERR",
        LogLevel.Success => "OK ",
        _ => "?"
    };

    public Brush LevelBrush => Level switch
    {
        LogLevel.Debug   => new SolidColorBrush(Color.FromRgb(0x9C, 0xA3, 0xAF)),
        LogLevel.Info    => new SolidColorBrush(Color.FromRgb(0x18, 0x18, 0x1B)),
        LogLevel.Warn    => new SolidColorBrush(Color.FromRgb(0xCA, 0x8A, 0x04)),
        LogLevel.Error   => new SolidColorBrush(Color.FromRgb(0xDC, 0x26, 0x26)),
        LogLevel.Success => new SolidColorBrush(Color.FromRgb(0x16, 0xA3, 0x4A)),
        _ => Brushes.Black,
    };

    public string DisplayText =>
        $"[{Time:HH:mm:ss.fff}] {LevelText} [{Source}] {Message}";
}

/// <summary>
/// 全局日志服务：单例 + 静态转发。
/// UI 订阅 LogAppended 事件实时显示；其他模块直接调 Logger.Info/Warn/Error。
///
/// 内存保留策略：最多保留 MaxEntries 条，超过自动剔除最早的。
///
/// 线程模型：ObservableCollection 自带 INotifyCollectionChanged，
/// 但跨线程 Add 会抛异常。LogAppended 事件在原线程触发，UI 端用
/// Dispatcher.BeginInvoke 切回 UI 线程再插入 ListBox 即可。
/// </summary>
public sealed class Logger : INotifyPropertyChanged
{
    public static Logger Instance { get; } = new Logger();

    private readonly ObservableCollection<LogEntry> _entries = new();
    /// <summary>UI 可绑定的日志集合</summary>
    public ObservableCollection<LogEntry> Entries => _entries;

    /// <summary>最大保留条数（防止无限增长吃内存）</summary>
    public const int MaxEntries = 1000;

    /// <summary>新日志加入时触发（同步，在原线程触发；UI 需自行 Dispatcher.Invoke）</summary>
    public event Action<LogEntry>? LogAppended;

    private int _totalCount;   // 累计日志数（含已淘汰的）
    public int TotalCount => _totalCount;

    public event PropertyChangedEventHandler? PropertyChanged;

    private Logger() { }

    /// <summary>
    /// 在 UI 线程把日志条目加入 Entries 集合（让 ListBox 自动刷新）。
    /// 通常由 MainWindow 订阅 LogAppended 后用 Dispatcher.BeginInvoke 调用。
    /// </summary>
    public void AddToEntriesOnUiThread(LogEntry entry)
    {
        _entries.Add(entry);
        while (_entries.Count > MaxEntries)
            _entries.RemoveAt(0);
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(TotalCount)));
    }

    private void Append(LogLevel level, string source, string message)
    {
        var entry = new LogEntry
        {
            Level = level,
            Source = source ?? "",
            Message = message ?? "",
        };
        _totalCount++;
        // 触发事件：让 UI 订阅者在自己的线程切回 UI 线程
        LogAppended?.Invoke(entry);
    }

    // ============================================================
    // 各级别便捷 API（静态转发到单例）
    // ============================================================
    public static void Debug(string source, string message)  => Instance.Append(LogLevel.Debug,   source, message);
    public static void Info(string source, string message)   => Instance.Append(LogLevel.Info,    source, message);
    public static void Warn(string source, string message)    => Instance.Append(LogLevel.Warn,    source, message);
    public static void Error(string source, string message)   => Instance.Append(LogLevel.Error,   source, message);
    public static void Success(string source, string message) => Instance.Append(LogLevel.Success, source, message);

    /// <summary>清空 UI 集合（Entries），但不清 TotalCount</summary>
    public void ClearEntries()
    {
        _entries.Clear();
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(TotalCount)));
        Append(LogLevel.Info, "Logger", "日志面板已清空");
    }
}
