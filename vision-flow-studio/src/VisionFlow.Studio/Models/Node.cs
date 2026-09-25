using System.ComponentModel;
using System.Runtime.CompilerServices;
using VisionFlow.Studio.TypeSystem;

namespace VisionFlow.Studio.Models;

/// <summary>
/// 节点实例（运行时）：画布上具体的一个节点。
/// 引用 NodeDescriptor 作为类型；实例有自己的 ID、位置、参数缓存。
///
/// 内存模型：
///   - Inputs/Outputs 在创建时根据 NodeDescriptor 一次性构造，运行时不变
///   - Parameters 是 mutable 字典，用户在属性面板修改
///   - ResultCache 由 DagEngine 写入，UI 仅读取
///
/// INotifyPropertyChanged：HasError/LastError 由 DagEngine 写入，
/// NodeControl 监听后切换标题栏红色边框/红色背景。
/// </summary>
public sealed class Node : INotifyPropertyChanged
{
    public Guid InstanceId { get; } = Guid.NewGuid();

    public string TypeId { get; }
    public string DisplayName { get; set; }
    public string Category { get; }
    public string AlgoName { get; }
    public string DllName { get; }    // 绑定的算法 DLL

    public double X { get; set; }
    public double Y { get; set; }

    public IReadOnlyList<Port> Inputs { get; }
    public IReadOnlyList<Port> Outputs { get; }

    public Dictionary<string, object?> Parameters { get; } = new();

    /// <summary>
    /// 运行时输出缓存：端口名 -> 执行结果（.NET 端强类型对象）
    /// 仅在 DagEngine 执行后填充；UI 通过此缓存做预览。
    /// </summary>
    public Dictionary<string, object?> OutputCache { get; } = new();

    /// <summary>
    /// 运行时输入缓存：输入端口名 -> 上游传过来的值。
    /// DagEngine 在把上游 OutputCache 数据推给本节点的同时写入此缓存；
    /// UI 通过此缓存显示"B 的输入端口当前值是 A 传过来的什么"。
    /// </summary>
    public Dictionary<string, object?> InputCache { get; } = new();

    // ============================================================
    // 错误态：DagEngine 在 RunNode 失败时置 HasError=true 并写 LastError；
    // 成功时清空。NodeControl 监听 PropertyChanged 切换红色视觉。
    // ============================================================
    private bool _hasError;
    private string? _lastError;

    public bool HasError
    {
        get => _hasError;
        set => SetField(ref _hasError, value);
    }

    public string? LastError
    {
        get => _lastError;
        set => SetField(ref _lastError, value);
    }

    public Node(string typeId, string displayName, string category,
                string algoName, string dllName,
                IReadOnlyList<Port> inputs, IReadOnlyList<Port> outputs)
    {
        TypeId = typeId;
        DisplayName = displayName;
        Category = category;
        AlgoName = algoName;
        DllName = dllName;
        Inputs = inputs;
        Outputs = outputs;
        foreach (var p in inputs) p.Owner = this;
        foreach (var p in outputs) p.Owner = this;
    }

    public Port? FindInput(string name)
        => Inputs.FirstOrDefault(p => p.Name == name);

    public Port? FindOutput(string name)
        => Outputs.FirstOrDefault(p => p.Name == name);

    public override string ToString() => $"{DisplayName} ({TypeId})";

    // ============================================================
    // INotifyPropertyChanged 实现
    // ============================================================
    public event PropertyChangedEventHandler? PropertyChanged;

    private void OnPropertyChanged([CallerMemberName] string? name = null)
    {
        var handler = PropertyChanged;
        if (handler is null) return;
        var args = new PropertyChangedEventArgs(name);
        // DAG 可在后台线程执行：属性变更会触发 NodeControl 刷 UI，
        // 必须调度回 UI 线程。用 BeginInvoke（异步）而非 Invoke（同步），
        // 避免后台线程与 UI 线程相互等待造成死锁/卡死。
        var disp = System.Windows.Application.Current?.Dispatcher;
        if (disp is not null && !disp.CheckAccess())
            disp.BeginInvoke(new Action(() => handler.Invoke(this, args)));
        else
            handler.Invoke(this, args);
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        OnPropertyChanged(name);
        return true;
    }
}
