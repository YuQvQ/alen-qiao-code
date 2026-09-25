using System.Windows;

namespace VisionFlow.Studio;

/// <summary>
/// 应用入口：初始化 DI 容器、节点注册表、互操作层
/// </summary>
public partial class App : Application
{
    public static App CurrentApp => (App)Current;

    /// <summary>
    /// 全局服务提供者（轻量 DI：构造时注入到 MainWindow）
    /// </summary>
    public IServiceProvider Services { get; private set; } = null!;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // 应用退出时自动保存当前工程（双重保险：Window.Closing + Application.Exit）
        // 某些关闭方式（如系统注销、进程退出）可能不触发 Window.Closing，
        // 但 Application.Exit 在正常退出路径上始终触发。
        Exit += OnApplicationExit;

        var window = new MainWindow();
        window.Show();
    }

    /// <summary>
    /// 应用退出事件：兜底自动保存当前工程到 autosave.vflow。
    /// MainWindow 实例通过 Application.Current.MainWindow 获取。
    /// </summary>
    private void OnApplicationExit(object sender, ExitEventArgs e)
    {
        try
        {
            if (Current?.MainWindow is MainWindow mw)
            {
                mw.AutoSave();
            }
        }
        catch
        {
            // 退出阶段异常静默处理，避免阻塞进程退出
        }
    }
}
