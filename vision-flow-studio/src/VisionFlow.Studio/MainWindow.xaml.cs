using System.IO;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using Microsoft.Win32;
using VisionFlow.Studio.Controls;
using VisionFlow.Studio.Execution;
using VisionFlow.Studio.Interop;
using VisionFlow.Studio.Models;
using VisionFlow.Studio.Nodes;
using VisionFlow.Studio.Templates;

namespace VisionFlow.Studio;

/// <summary>
/// 主窗口：左侧节点库 / 中央画布 / 右侧属性 / 顶部工具栏
/// 数据流：
///   NodeRegistry -> 节点库 ItemsSource
///   画布拖拽 -> NodeInstance 加入 Graph
///   Graph 选中节点 -> 属性面板
///   BtnRun -> DagEngine.Run(Graph)
/// </summary>
public partial class MainWindow : Window
{
    private readonly Graph _graph = new();
    private DagEngine? _engine;
    private System.ComponentModel.ICollectionView? _nodeView;

    public MainWindow()
    {
        InitializeComponent();
        Loaded += OnLoaded;
        Closing += OnClosing;
        // 订阅 Logger：跨线程时切回 UI 线程把日志插入 ListBox
        Logger.Instance.LogAppended += OnLogAppended;
    }

    /// <summary>窗口关闭时自动保存当前方案到 autosave.vflow</summary>
    private void OnClosing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        _runCts?.Cancel();
        TcpNodeRuntime.StopAll();
        AutoSave();
    }

    /// <summary>
    /// 自动保存当前工程到 autosave.vflow。
    /// - 画布非空：序列化保存
    /// - 画布为空：删除旧的 autosave.vflow（避免下次启动加载过期工程）
    /// 该方法由 Window.Closing 和 Application.Exit 双重调用，确保退出时一定保存。
    /// </summary>
    public void AutoSave()
    {
        try
        {
            if (_graph.Nodes.Count > 0)
            {
                if (SaveToFile(AutoSavePath))
                    Logger.Info("AutoSave", $"已自动保存方案到 '{AutoSavePath}'（{_graph.Nodes.Count} 节点 / {_graph.Edges.Count} 连线）");
                else
                    Logger.Error("AutoSave", "自动保存失败（SaveToFile 返回 false）");
            }
            else
            {
                // 画布为空：删除旧的 autosave 文件，避免下次启动加载过期工程
                if (File.Exists(AutoSavePath))
                {
                    File.Delete(AutoSavePath);
                    Logger.Info("AutoSave", "画布为空，已删除旧的 autosave.vflow");
                }
            }
        }
        catch (Exception ex)
        {
            Logger.Error("AutoSave", $"自动保存异常：{ex.Message}");
        }
    }

    private void OnLogAppended(LogEntry entry)
    {
        if (!Dispatcher.CheckAccess())
            Dispatcher.BeginInvoke(new Action<LogEntry>(OnLogAppended), entry);
        else
        {
            Logger.Instance.AddToEntriesOnUiThread(entry);
            LogCountText.Text = $"{Logger.Instance.TotalCount} 条";
            // 自动滚动到底（如果用户勾选）
            if (AutoScrollCheck.IsChecked == true && LogList.Items.Count > 0)
            {
                LogList.ScrollIntoView(LogList.Items[LogList.Items.Count - 1]);
            }
        }
    }

    private void BtnClearLog_Click(object sender, RoutedEventArgs e)
    {
        Logger.Instance.ClearEntries();
        LogCountText.Text = $"{Logger.Instance.TotalCount} 条";
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Logger.Info("App", "VisionFlow.Studio 启动中…");
        // 1) 加载节点描述：先扫 nodes/*.json 离线装载
        var nodesDir = Path.Combine(AppContext.BaseDirectory, "nodes");
        // 调试/开发期：从源码 nodes/ 目录兜底（部署后会被 csproj 复制到 bin output）
        if (!Directory.Exists(nodesDir))
        {
            var devDir = Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "nodes");
            if (Directory.Exists(devDir)) nodesDir = devDir;
        }
        NodeRegistry.Instance.LoadFromDirectory(nodesDir);
        Logger.Info("NodeRegistry", $"从 '{nodesDir}' 加载了 {NodeRegistry.Instance.All.Count} 个节点描述");

        // 注册 C# 托管节点：TCP 服务端 / 客户端（不在 C++ DLL 中，运行时由 DagEngine 特殊处理）
        RegisterTcpNodes();

        // 2) 填充节点库
        // 按分类分组显示节点库
        var nodes = NodeRegistry.Instance.All.ToList();
        var view = (System.Windows.Data.CollectionView)System.Windows.Data.CollectionViewSource.GetDefaultView(nodes);
        view.GroupDescriptions.Add(new System.Windows.Data.PropertyGroupDescription("Category"));
        _nodeView = view;
        NodeLibraryList.ItemsSource = view;

        // 3) 画布绑定 Graph + 订阅画布上报事件
        CanvasHost.Graph = _graph;
        CanvasHost.SelectedNodeChanged += OnSelectedNodeChanged;
        CanvasHost.StatusText += msg => StatusText.Text = msg;
        CanvasHost.NodeDropped += OnNodeDropped;
        CanvasHost.NodeDoubleClicked += OnNodeDoubleClicked;
        CanvasHost.NodeRunRequested += OnNodeRunRequested;
        CanvasHost.NodeHelpRequested += OnNodeHelpRequested;

        // 4) 执行引擎
        _engine = new DagEngine(_graph);
        Logger.Info("DagEngine", "DAG 引擎就绪");

        // 订阅 ImageHost 的 ROI 变化事件：用户在图像窗口拖拽 ROI 后，
        // 刷新画布悬浮标签，使参数值显示最新值
        ImageHost.RoiChanged += () =>
        {
            CanvasHost.RefreshFloatingLabels();
        };

        // 模板管理：导入/导出/新建
        ImageHost.ImportTemplateRequested += OnImportTemplate;
        ImageHost.ExportTemplateRequested += OnExportTemplate;
        ImageHost.NewTemplateDrawn += OnNewTemplateDrawn;
        ImageHost.NewTemplateEditorRequested += OnNewTemplateEditor;

        StatusText.Text = $"已加载 {NodeRegistry.Instance.All.Count} 个节点描述，可从左侧拖拽节点到画布";

        // 自动加载上次关闭时保存的方案
        if (File.Exists(AutoSavePath))
        {
            try
            {
                if (LoadFromFile(AutoSavePath))
                {
                    StatusText.Text = $"已自动恢复上次方案（{_graph.Nodes.Count} 节点 / {_graph.Edges.Count} 连线）";
                    Logger.Info("AutoLoad", $"已自动加载上次方案 '{AutoSavePath}'");
                }
                else
                {
                    StatusText.Text = "自动恢复上次方案失败，请查看日志面板（AutoLoad）";
                    Logger.Warn("AutoLoad", $"自动加载方案失败，文件 '{AutoSavePath}' 可能已损坏");
                }
            }
            catch (Exception ex)
            {
                StatusText.Text = "自动恢复上次方案失败，请查看日志面板（AutoLoad）";
                Logger.Warn("AutoLoad", $"自动加载方案异常（忽略）：{ex.Message}");
            }
        }
    }

    // ============================================================
    // 节点库：搜索过滤
    // ============================================================
    private void NodeSearchBox_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (_nodeView is null) return;
        var kw = (NodeSearchBox.Text ?? "").Trim().ToLowerInvariant();
        _nodeView.Filter = string.IsNullOrEmpty(kw) ? null : new Predicate<object>(o =>
        {
            if (o is not NodeDescriptor d) return true;
            return (d.DisplayName ?? "").ToLowerInvariant().Contains(kw)
                || (d.TypeId ?? "").ToLowerInvariant().Contains(kw)
                || (d.Category ?? "").ToLowerInvariant().Contains(kw);
        });
    }

    // ============================================================
    // 节点库：PreviewMouseDown 启动拖拽（按住左键移动即触发）
    // ============================================================
    private void NodeLibraryList_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not ListBox lb) return;
        // 从事件原始源向上遍历可视树，找到 ListBoxItem
        // 分组模式下 ListBoxItem 嵌套在 Expander > ItemsPresenter 内
        var hit = e.OriginalSource as DependencyObject;
        while (hit is not null && hit is not ListBoxItem)
            hit = VisualTreeHelper.GetParent(hit);
        if (hit is ListBoxItem item)
        {
            // 直接取 DataContext；分组模式下也是 NodeDescriptor
            var desc = item.DataContext as NodeDescriptor
                       ?? lb.ItemContainerGenerator.ItemFromContainer(item) as NodeDescriptor;
            if (desc is not null)
            {
                NodeCanvas.BeginDragNodeDescriptor(item, desc);
                e.Handled = true;
            }
        }
    }

    // ============================================================
    // 节点库拖到画布 -> 在指定位置创建节点实例
    // ============================================================
    private void OnNodeDropped(NodeDescriptor desc, Point pos)
    {
        try
        {
            // 居中放置：节点宽 180，高约 80，让鼠标落点在节点中心
            var node = NodeFactory.Create(desc,
                x: Math.Max(0, pos.X - 90),
                y: Math.Max(0, pos.Y - 40));
            _graph.AddNode(node);
            StatusText.Text = $"已创建节点：{node.DisplayName}";
            Logger.Info("Canvas", $"创建节点 '{node.DisplayName}' (type_id={node.TypeId}) @ ({node.X:F0},{node.Y:F0})");
        }
        catch (Exception ex)
        {
            StatusText.Text = $"创建失败：{ex.Message}";
            Logger.Error("Canvas", $"创建节点失败 type_id={desc.TypeId}: {ex.Message}");
        }
    }

    // ============================================================
    // 选中节点变化 -> 显示节点图像到图像窗口
    // 悬浮属性标签由 NodeCanvas 自行渲染（画布持有 SelectedNode）
    // ============================================================
    private void OnSelectedNodeChanged(Node? node)
    {
        if (node is null)
        {
            // 取消选中：清除图像窗口的 ROI 覆盖层
            ImageHost.ActiveNode = null;
            return;
        }
        // 选中节点：把输出图像绘制到图像窗口
        TryShowImageOutputToHost(node);
    }

    // ============================================================
    // 双击节点 -> 弹出参数编辑对话框
    // ============================================================
    private void OnNodeDoubleClicked(NodeControl ctrl)
    {
        var dlg = new NodeParameterDialog(ctrl.Node)
        {
            Owner = this,
            OnRoiDrawRequested = StartRoiDraw,
        };
        dlg.ShowDialog();
        // 关闭后刷新画布悬浮标签（参数可能已改）
        CanvasHost.RefreshFloatingLabels();
    }

    /// <summary>
    /// 用户在参数对话框点击"绘制..."按钮：确保图像已加载到 ImageHost，
    /// 然后进入主图像窗口的 ROI 绘制模式。
    /// </summary>
    private void StartRoiDraw(Node node, string paramName)
    {
        // 找到 image 端口的上游，跑一遍拿图
        var imagePort = node.FindInput("image");
        if (imagePort is null)
        {
            StatusText.Text = "该节点没有 image 输入端口，无法绘制 ROI";
            Logger.Warn("RoiDraw", $"节点 '{node.DisplayName}' 没有 image 输入端口");
            return;
        }
        var edge = _graph.EdgesInto(node).FirstOrDefault(e => e.To.Name == "image");
        if (edge is null)
        {
            StatusText.Text = "image 端口未连接上游，请先连接图像源";
            Logger.Warn("RoiDraw", $"节点 '{node.DisplayName}' 的 image 端口未连接上游");
            return;
        }
        var upstream = edge.From.Owner;
        try
        {
            Logger.Info("RoiDraw", $"节点 '{node.DisplayName}' 绘制 {paramName}：先跑上游 '{upstream.DisplayName}' 拿图");
            if (_engine is null) return;
            var upRes = _engine.RunNodeWithUpstream(upstream);
            if (upRes.AllSucceeded is false)
            {
                var f = upRes.Results.FirstOrDefault(r => !r.Success);
                StatusText.Text = $"上游运行失败：{f?.Error}";
                Logger.Error("RoiDraw", $"上游 '{upstream.DisplayName}' 运行失败：{f?.Error}");
                return;
            }
            if (!upstream.OutputCache.TryGetValue("image", out var data) || data is null
                || data is not VzImage img || img.data == IntPtr.Zero)
            {
                StatusText.Text = "上游没有可显示的图像数据";
                Logger.Warn("RoiDraw", $"上游 '{upstream.DisplayName}' 没有可用图像数据");
                return;
            }
            Logger.Info("RoiDraw", $"上游图像已就绪：{img.width}x{img.height} channels={img.channels}");

            // 显示到图像窗口并进入绘制模式
            ImageHost.ShowNodeImage(node, ref img, sourceLabel: $"{upstream.DisplayName}.image");
            VzAlgoInterop.FreeVzImageData(img);
            upstream.OutputCache["image"] = default(VzImage);

            // 从节点描述读取该参数的 ROI 形状
            var desc = NodeRegistry.Instance.Get(node.TypeId);
            if (desc is null) return;
            var pd = desc.Parameters.FirstOrDefault(p => p.Name == paramName);
            var shape = pd?.GetRoiShape() ?? "rect";
            ImageHost.StartDrawMode(paramName, shape);
            StatusText.Text = $"请在右侧图像窗口拖拽绘制 {paramName}（{shape}）";
        }
        catch (Exception ex)
        {
            StatusText.Text = $"ROI 绘制准备失败：{ex.Message}";
            Logger.Error("RoiDraw", $"ROI 绘制准备失败：{ex.Message}");
        }
    }

    /// <summary>点击节点"?"按钮 → 弹出使用说明窗口</summary>
    private void OnNodeHelpRequested(NodeControl ctrl)
    {
        var dlg = new NodeHelpDialog(ctrl.Node) { Owner = this };
        dlg.ShowDialog();
    }

    /// <summary>
    /// 如果节点 OutputCache 里有 image 输出端口，把第一张图绘制到 ImageHost。
    /// 用于：选中节点时自动预览；运行完成后立即绘制。
    /// </summary>
    private void TryShowImageOutputToHost(Node? node)
    {
        if (node is null)
        {
            ImageHost.ActiveNode = null;
            return;
        }

        // 把当前节点关联到图像窗口，使 ROI 参数可被渲染为可拖拽覆盖层
        ImageHost.ActiveNode = node;

        // 端口查找
        var matchesPort = node.Outputs.FirstOrDefault(p => p.Type.Name == "MatchResultList");
        var rectsPort = node.Outputs.FirstOrDefault(p => p.Type.Name == "RectList");
        var imgPort = node.Outputs.FirstOrDefault(p => p.Type.Name == "Image");

        // 优先显示节点自身的 result_image（现已是原图透传，无标注），
        // 再由 UI 层 OverlayRenderer 依据节点输出数据绘制覆盖标注。
        var resultImgPort = node.Outputs.FirstOrDefault(p => p.Name == "result_image");
        if (resultImgPort is not null
            && node.OutputCache.TryGetValue(resultImgPort.Name, out var riVal)
            && riVal is VzImage rImg && rImg.data != IntPtr.Zero)
        {
            ImageHost.ShowImageWithOverlay(ref rImg, node,
                sourceLabel: $"{node.DisplayName}.result_image");
            return;
        }

        // 取上游 image（不管本节点有没有 image 输出，都尝试找上游原图）
        VzImage? upImg = null;
        string? upLabel = null;
        var imageInputPort = node.FindInput("image");
        if (imageInputPort is not null)
        {
            var edge = _graph.EdgesInto(node).FirstOrDefault(e => e.To.Name == "image");
            if (edge is not null)
            {
                var upstream = edge.From.Owner;
                if (upstream.OutputCache.TryGetValue(edge.From.Name, out var upVal)
                    && upVal is VzImage ui && ui.data != IntPtr.Zero)
                {
                    upImg = ui;
                    upLabel = $"{upstream.DisplayName}.{edge.From.Name}";
                }
            }
        }

        // 优先用上游图像 + 叠加结果（更符合"在原图上看结果"的需求）
        if (upImg is VzImage uimg)
        {
            // 模板匹配结果叠加到上游原图
            if (matchesPort is not null
                && node.OutputCache.TryGetValue(matchesPort.Name, out var mVal)
                && mVal is VzMatchResult[] matches && matches.Length > 0)
            {
                ImageHost.ShowImageWithMatches(ref uimg, matches,
                    sourceLabel: $"{node.DisplayName}.{matchesPort.Name} (image from {upLabel})");
                return;
            }
            // RectList 结果叠加到上游原图
            if (rectsPort is not null
                && node.OutputCache.TryGetValue(rectsPort.Name, out var rVal)
                && rVal is VzRect[] rects && rects.Length > 0)
            {
                ImageHost.ShowImageWithRects(ref uimg, rects,
                    sourceLabel: $"{node.DisplayName}.{rectsPort.Name} (image from {upLabel})");
                return;
            }
            // 有上游图：在上游原图上由 UI 层绘制节点覆盖标注
            ImageHost.ShowImageWithOverlay(ref uimg, node, sourceLabel: upLabel ?? "");
            return;
        }

        // 没有上游图时，回退到节点自身的 image 输出
        if (imgPort is not null
            && node.OutputCache.TryGetValue(imgPort.Name, out var val)
            && val is VzImage img && img.data != IntPtr.Zero)
        {
            VzMatchResult[]? matches = null;
            if (matchesPort is not null
                && node.OutputCache.TryGetValue(matchesPort.Name, out var mVal)
                && mVal is VzMatchResult[] arr)
                matches = arr;

            if (matches is { Length: > 0 })
                ImageHost.ShowImageWithMatches(ref img, matches,
                    sourceLabel: $"{node.DisplayName}.{imgPort.Name}");
            else
                ImageHost.ShowImageWithOverlay(ref img, node,
                    sourceLabel: $"{node.DisplayName}.{imgPort.Name}");
        }
    }

    // ============================================================
    // 模板管理：导入 / 导出 / 新建
    // ============================================================

    /// <summary>导入模板：从 .tpl 文件加载，设置为节点的 template 输入。</summary>
    private void OnImportTemplate(Node? node)
    {
        if (node is null || node.TypeId != "TemplateMatch")
        {
            StatusText.Text = "请先选中模板匹配节点";
            return;
        }
        var ofd = new OpenFileDialog
        {
            Title = "导入模板文件",
            Filter = "模板文件|*.tpl|所有文件|*.*",
            CheckFileExists = true,
        };
        if (ofd.ShowDialog() != true) return;
        try
        {
            var (tplImg, tplMask) = TemplateManager.LoadTemplate(ofd.FileName);
            if (tplImg.data == IntPtr.Zero)
            {
                StatusText.Text = "模板文件加载失败";
                return;
            }
            // 设置到节点的 template 端口输入缓存（跳过 image 端口连接）
            node.InputCache["template"] = tplImg;
            if (tplMask.data != IntPtr.Zero)
                node.InputCache["template_mask"] = tplMask;
            else
                node.InputCache.Remove("template_mask");
            node.Parameters["template_roi"] = "";  // 清空 template_roi，改用 template 端口输入
            StatusText.Text = $"已导入模板：{ofd.FileName}（{tplImg.width}×{tplImg.height}）";
            Logger.Info("Template", $"导入模板 '{ofd.FileName}'：{tplImg.width}x{tplImg.height}");
            ImageHost.RenderRoiOverlays();
            CanvasHost.RefreshFloatingLabels();
        }
        catch (Exception ex)
        {
            StatusText.Text = $"导入模板失败：{ex.Message}";
            Logger.Error("Template", $"导入模板失败：{ex.Message}");
        }
    }

    /// <summary>导出模板：把当前 template 图像保存为 .tpl 文件。</summary>
    private void OnExportTemplate(Node? node)
    {
        if (node is null || node.TypeId != "TemplateMatch")
        {
            StatusText.Text = "请先选中模板匹配节点";
            return;
        }
        // 从节点缓存获取模板图像
        if (!node.InputCache.TryGetValue("template", out var tpl) || tpl is not VzImage tplImg || tplImg.data == IntPtr.Zero)
        {
            StatusText.Text = "当前没有模板图像可导出（请先新建或导入模板）";
            return;
        }
        var sfd = new SaveFileDialog
        {
            Title = "导出模板文件",
            Filter = "模板文件|*.tpl|所有文件|*.*",
            FileName = "template.tpl",
        };
        if (sfd.ShowDialog() != true) return;
        try
        {
            double[]? roi = null;
            if (node.Parameters.TryGetValue("template_roi", out var rv) && rv is string rs && !string.IsNullOrWhiteSpace(rs))
                roi = ParseRoiDoubles(rs);
            VzImage? mask = null;
            if (node.InputCache.TryGetValue("template_mask", out var mv) && mv is VzImage mi && mi.data != IntPtr.Zero)
                mask = mi;
            TemplateManager.SaveTemplate(sfd.FileName, ref tplImg, roi, mask);
            StatusText.Text = $"已导出模板：{sfd.FileName}";
            Logger.Info("Template", $"导出模板到 '{sfd.FileName}'");
        }
        catch (Exception ex)
        {
            StatusText.Text = $"导出模板失败：{ex.Message}";
            Logger.Error("Template", $"导出模板失败：{ex.Message}");
        }
    }

    /// <summary>新建模板：用户在图像上框选矩形 ROI 后，裁剪图像生成模板。</summary>
    private void OnNewTemplateDrawn(double x, double y, double w, double h)
    {
        if (CanvasHost.SelectedNode is not Node node || node.TypeId != "TemplateMatch")
        {
            StatusText.Text = "请先选中模板匹配节点再新建模板";
            return;
        }
        // 找到上游图像
        var edge = _graph.EdgesInto(node).FirstOrDefault(e => e.To.Name == "image");
        if (edge is null)
        {
            StatusText.Text = "image 端口未连接上游";
            return;
        }
        var upstream = edge.From.Owner;
        try
        {
            if (_engine is null) return;
            var upRes = _engine.RunNodeWithUpstream(upstream);
            if (!upRes.AllSucceeded) { StatusText.Text = "上游运行失败"; return; }
            if (!upstream.OutputCache.TryGetValue("image", out var data) || data is not VzImage img || img.data == IntPtr.Zero)
            {
                StatusText.Text = "上游没有图像数据";
                return;
            }
            // 裁剪 ROI 区域生成模板
            var tplImg = TemplateManager.CreateTemplate(ref img, new[] { x, y, w, h });
            if (tplImg.data == IntPtr.Zero)
            {
                StatusText.Text = "模板裁剪失败";
                VzAlgoInterop.FreeVzImageData(img);
                upstream.OutputCache["image"] = default(VzImage);
                return;
            }
            node.InputCache["template"] = tplImg;
            // 同时把 template_roi 设置为裁剪区域（以 [x,y,w,h] 格式）
            node.Parameters["template_roi"] = $"[{x:F1},{y:F1},{w:F1},{h:F1}]";
            VzAlgoInterop.FreeVzImageData(img);
            upstream.OutputCache["image"] = default(VzImage);

            StatusText.Text = $"已新建模板：{tplImg.width}×{tplImg.height}（来源 ROI [{x:F0},{y:F0},{w:F0},{h:F0}]）";
            Logger.Info("Template", $"新建模板：{tplImg.width}x{tplImg.height}，ROI=[{x},{y},{w},{h}]");
            ImageHost.RenderRoiOverlays();
            CanvasHost.RefreshFloatingLabels();
        }
        catch (Exception ex)
        {
            StatusText.Text = $"新建模板失败：{ex.Message}";
            Logger.Error("Template", $"新建模板失败：{ex.Message}");
        }
    }

    /// <summary>打开独立模板编辑器窗口，基于预处理图像创建模板+掩膜。</summary>
    private void OnNewTemplateEditor(Node? node)
    {
        if (node is null || node.TypeId != "TemplateMatch") return;
        if (_engine is null) return;

        // 找到上游图像并运行
        var edge = _graph.EdgesInto(node).FirstOrDefault(e => e.To.Name == "image");
        if (edge is null) { StatusText.Text = "image 端口未连接上游"; return; }
        var upstream = edge.From.Owner;
        try
        {
            var upRes = _engine.RunNodeWithUpstream(upstream);
            if (!upRes.AllSucceeded) { StatusText.Text = "上游运行失败"; return; }
            if (!upstream.OutputCache.TryGetValue("image", out var data) || data is not VzImage img || img.data == IntPtr.Zero)
            { StatusText.Text = "上游没有图像数据"; return; }

            // 打开模板编辑器
            var editor = new TemplateEditorWindow(img) { Owner = this };
            if (editor.ShowDialog() == true)
            {
                node.InputCache["template"] = editor.ResultImage;
                if (editor.ResultMask.data != IntPtr.Zero)
                    node.InputCache["template_mask"] = editor.ResultMask;
                else
                    node.InputCache.Remove("template_mask");
                if (editor.ResultRoi is { Length: >= 4 })
                    node.Parameters["template_roi"] = $"[{editor.ResultRoi[0]:F1},{editor.ResultRoi[1]:F1},{editor.ResultRoi[2]:F1},{editor.ResultRoi[3]:F1}]";

                StatusText.Text = $"模板已创建：{editor.ResultImage.width}×{editor.ResultImage.height}" +
                    (editor.ResultMask.data != IntPtr.Zero ? "（含掩膜）" : "");
                Logger.Info("Template", $"模板编辑器创建模板 {editor.ResultImage.width}x{editor.ResultImage.height}");
                ImageHost.RenderRoiOverlays();
                CanvasHost.RefreshFloatingLabels();
            }
            VzAlgoInterop.FreeVzImageData(img);
            upstream.OutputCache["image"] = default(VzImage);
        }
        catch (Exception ex)
        {
            StatusText.Text = $"模板编辑器失败：{ex.Message}";
            Logger.Error("Template", $"模板编辑器失败：{ex.Message}");
        }
    }

    // ============================================================
    // 节点上的"▶ 运行"按钮 -> 仅跑该节点 + 上游依赖
    // ============================================================
    private void OnNodeRunRequested(NodeControl ctrl)
    {
        if (_engine is null) return;
        try
        {
            StatusText.Text = $"运行节点：{ctrl.Node.DisplayName}（含上游）…";
            Logger.Info("Run", $"▶ 单独运行 '{ctrl.Node.DisplayName}'（含上游依赖）");
            var result = _engine.RunNodeWithUpstream(ctrl.Node);
            StatusText.Text = $"完成：{result.Succeeded}/{result.Results.Count} 成功" +
                              (result.Skipped > 0 ? $"，{result.Skipped} 跳过" : "") +
                              (result.Failed > 0 ? $"，{result.Failed} 失败" : "");
            // 首个"失败"（skipped 不算失败，不在此显示）
            var firstFail = result.Results.FirstOrDefault(r => !r.Success && !r.Skipped);
            if (firstFail is not null)
            {
                StatusText.Text += $" | 首个错误：{firstFail.Error}";
                Logger.Error("Run", $"节点 '{firstFail.Node.DisplayName}' 失败：{firstFail.Error}");
            }
            else
            {
                Logger.Success("Run", $"节点 '{ctrl.Node.DisplayName}' 运行完成，{result.Succeeded} 成功" +
                                  (result.Skipped > 0 ? $"，{result.Skipped} 跳过" : ""));
            }
            // 运行后刷新画布悬浮标签 + 绘制图像
            CanvasHost.RefreshFloatingLabels();
            TryShowImageOutputToHost(ctrl.Node);
        }
        catch (Exception ex)
        {
            StatusText.Text = $"运行异常：{ex.Message}";
            Logger.Error("Run", $"运行异常：{ex.Message}");
        }
    }

    // ============================================================
    // 注册 C# 托管 TCP 节点（运行时加入节点库）
    // ============================================================
    private static System.Text.Json.JsonElement JEl(string raw)
        => System.Text.Json.JsonDocument.Parse(raw).RootElement;

    private void RegisterTcpNodes()
    {
        var server = new NodeDescriptor
        {
            TypeId = "TcpServer",
            DisplayName = "TCP服务端",
            Category = "通信",
            AlgoBinding = new AlgoBindingDescriptor { Dll = "managed", AlgoName = "TcpServer" },
            Outputs = new List<PortDescriptor>
            {
                new() { Name = "message", Type = "String", Required = true, Description = "收到的一条消息" },
            },
            Parameters = new List<ParameterDescriptor>
            {
                new() { Name = "ip", Type = "String", Default = JEl("\"0.0.0.0\""), Description = "监听IP(0.0.0.0=任意)" },
                new() { Name = "port", Type = "Int", Default = JEl("8080"), Min = 1, Max = 65535, Description = "监听端口" },
                new() { Name = "trigger", Type = "String", Default = JEl("\"\""), Description = "触发字符串(可选)" },
                new() { Name = "terminator", Type = "String", Default = JEl("\"\\n\""), Description = "消息结束符" },
            },
        };
        var client = new NodeDescriptor
        {
            TypeId = "TcpClient",
            DisplayName = "TCP客户端",
            Category = "通信",
            AlgoBinding = new AlgoBindingDescriptor { Dll = "managed", AlgoName = "TcpClient" },
            Inputs = new List<PortDescriptor>
            {
                new() { Name = "send", Type = "String", Required = false, Description = "要发送的内容(可选)" },
            },
            Outputs = new List<PortDescriptor>
            {
                new() { Name = "message", Type = "String", Required = true, Description = "收到的一条消息" },
            },
            Parameters = new List<ParameterDescriptor>
            {
                new() { Name = "ip", Type = "String", Default = JEl("\"127.0.0.1\""), Description = "服务器IP" },
                new() { Name = "port", Type = "Int", Default = JEl("8080"), Min = 1, Max = 65535, Description = "服务器端口" },
                new() { Name = "trigger", Type = "String", Default = JEl("\"hello\\n\""), Description = "连接后发送的触发字符串" },
                new() { Name = "terminator", Type = "String", Default = JEl("\"\\n\""), Description = "消息结束符" },
            },
        };
        NodeRegistry.Instance.Register(server);
        NodeRegistry.Instance.Register(client);
        RegisterIfNode();
    }

    /// <summary>注册 If 条件节点：输入 obj(任意类型)，条件成立则原样透传到 out。</summary>
    private void RegisterIfNode()
    {
        var ifNode = new NodeDescriptor
        {
            TypeId = "If",
            DisplayName = "条件判断",
            Category = "逻辑",
            AlgoBinding = new AlgoBindingDescriptor { Dll = "managed", AlgoName = "If" },
            Inputs = new List<PortDescriptor>
            {
                new() { Name = "obj", Type = "Any", Required = true, Description = "输入数据（任意类型）" },
            },
            Outputs = new List<PortDescriptor>
            {
                new() { Name = "true",  Type = "Any", Required = true, Description = "条件成立时输出（类型随输入）" },
                new() { Name = "false", Type = "Any", Required = true, Description = "条件不成立时输出（类型随输入）" },
            },
            Parameters = new List<ParameterDescriptor>
            {
                new() { Name = "op", Type = "String", Default = JEl("\"nonempty\""),
                    Description = "判断方式：nonempty(非空/有数据) / == / != / > / >= / < / <=" },
                new() { Name = "value", Type = "String", Default = JEl("\"\""),
                    Description = "比较基准值（仅比较类 op 生效；按输入实际类型解析）" },
            },
        };
        NodeRegistry.Instance.Register(ifNode);
    }

    // ============================================================
    // 运行：连续运行模式（点击▶开始循环，■停止结束；停止态下不运行）
    // ============================================================
    private System.Threading.CancellationTokenSource? _runCts;

    private async void BtnRun_Click(object sender, RoutedEventArgs e)
    {
        if (_engine is null || _runCts is not null) return;
        if (_graph.Nodes.Count == 0)
        {
            StatusText.Text = "画布为空，无法运行";
            Logger.Error("Run", "画布为空（0 节点），已阻止连续运行");
            MessageBox.Show("画布上没有任何节点，无法运行。请先添加节点。", "无法运行", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        _runCts = new System.Threading.CancellationTokenSource();
        var ct = _runCts.Token;
        BtnRun.IsEnabled = false;
        BtnRunOnce.IsEnabled = false;
        BtnStop.IsEnabled = true;
        Mouse.OverrideCursor = Cursors.Wait;
        int iter = 0;
        try
        {
            StatusText.Text = "连续运行中…（点击 ■停止 结束）";
            Logger.Info("Run", $"===== 开始连续运行 DAG（{_graph.Nodes.Count} 节点）=====");
            while (!ct.IsCancellationRequested)
            {
                var sw = System.Diagnostics.Stopwatch.StartNew();
                var result = await System.Threading.Tasks.Task.Run(() => _engine.Run(ct), ct);
                sw.Stop();
                iter++;
                var firstFail = result.Results.FirstOrDefault(r => !r.Success && !r.Skipped);
                StatusText.Text = $"连续运行 第 {iter} 轮：{result.Succeeded}/{result.Results.Count} 成功"
                    + (result.Failed > 0 ? $"，{result.Failed} 失败" : "")
                    + (firstFail is not null ? $" | {firstFail.Error}" : "")
                    + $" · {sw.ElapsedMilliseconds}ms";
                if (iter % 3 == 0 || firstFail is not null)   // 不必每轮都刷悬浮标签
                    CanvasHost.RefreshFloatingLabels();
                ShowRunResultImage();
                await System.Threading.Tasks.Task.Delay(LoopIntervalMs(), ct);
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex)
        {
            StatusText.Text = $"运行异常：{ex.Message}";
            Logger.Error("Run", $"连续运行异常：{ex.Message}");
        }
        finally
        {
            Mouse.OverrideCursor = null;
            BtnRun.IsEnabled = true;
            BtnRunOnce.IsEnabled = true;
            BtnStop.IsEnabled = false;
            _runCts?.Dispose();
            _runCts = null;
            StatusText.Text = $"已停止（共运行 {iter} 轮）";
        }
    }

    /// <summary>运行一遍全流程（单次）。</summary>
    private async void BtnRunOnce_Click(object sender, RoutedEventArgs e)
    {
        if (_engine is null || _runCts is not null) return;   // 连续运行中不并发
        if (_graph.Nodes.Count == 0)
        {
            StatusText.Text = "画布为空，无法运行";
            Logger.Error("Run", "画布为空（0 节点），已阻止运行一次");
            MessageBox.Show("画布上没有任何节点，无法运行。请先添加节点。", "无法运行", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }
        BtnRun.IsEnabled = false;
        BtnRunOnce.IsEnabled = false;
        Mouse.OverrideCursor = Cursors.Wait;
        try
        {
            StatusText.Text = "运行一遍…";
            Logger.Info("Run", $"===== 运行一遍 DAG（{_graph.Nodes.Count} 节点）=====");
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var result = await System.Threading.Tasks.Task.Run(() => _engine.Run());
            sw.Stop();
            var firstFail = result.Results.FirstOrDefault(r => !r.Success && !r.Skipped);
            StatusText.Text = $"完成：{result.Succeeded}/{result.Results.Count} 成功"
                + (result.Failed > 0 ? $"，{result.Failed} 失败" : "")
                + (firstFail is not null ? $" | {firstFail.Error}" : "")
                + $" · {sw.ElapsedMilliseconds}ms";
            CanvasHost.RefreshFloatingLabels();
            ShowRunResultImage();
        }
        catch (Exception ex)
        {
            StatusText.Text = $"运行异常：{ex.Message}";
            Logger.Error("Run", $"运行一遍异常：{ex.Message}");
        }
        finally
        {
            Mouse.OverrideCursor = null;
            BtnRun.IsEnabled = true;
            BtnRunOnce.IsEnabled = true;
        }
    }

    private void BtnStop_Click(object sender, RoutedEventArgs e)
    {
        if (_runCts is null) return;
        StatusText.Text = "正在停止…";
        _runCts.Cancel();
    }

    /// <summary>每轮间隔：优先取图中 ForLoop 节点的 interval_ms，否则默认 30ms。</summary>
    private int LoopIntervalMs()
    {
        int iv = 30;
        foreach (var n in _graph.Nodes)
        {
            if (n.AlgoName != "ForLoop") continue;
            if (n.Parameters.TryGetValue("interval_ms", out var v) && v is not null
                && int.TryParse(v.ToString(), out var im) && im > iv) iv = im;
        }
        return iv;
    }

    /// <summary>把选中节点（或最后一个有图像输出的节点）的结果显示到图像窗口。</summary>
    private void ShowRunResultImage()
    {
        if (CanvasHost.SelectedNode is Node sel) TryShowImageOutputToHost(sel);
        else
        {
            var imgNode = _graph.Nodes.LastOrDefault(n =>
                n.OutputCache.Values.OfType<VzImage>().Any(v => v.data != IntPtr.Zero));
            if (imgNode is not null) TryShowImageOutputToHost(imgNode);
        }
    }

    private void BtnFit_Click(object sender, RoutedEventArgs e) => CanvasHost.FitToContent();
    private void BtnActual_Click(object sender, RoutedEventArgs e) => CanvasHost.ResetView();

    // ============================================================
    // 打开：反序列化 .vflow JSON 文件，重建节点 + 连线
    // ============================================================
    private void BtnOpen_Click(object sender, RoutedEventArgs e)
    {
        var ofd = new OpenFileDialog
        {
            Title = "打开流程图",
            Filter = "VisionFlow 流程图|*.vflow|JSON 文件|*.json|所有文件|*.*",
            CheckFileExists = true,
        };
        if (ofd.ShowDialog() != true) return;

        if (LoadFromFile(ofd.FileName))
            StatusText.Text = $"已加载：{ofd.FileName}（{_graph.Nodes.Count} 节点 / {_graph.Edges.Count} 连线）";
    }

    /// <summary>从指定路径反序列化 .vflow 文件，重建节点 + 连线。成功返回 true。</summary>
    private bool LoadFromFile(string path)
    {
        try
        {
            var json = File.ReadAllText(path);
            var options = new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true,
                Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
            };
            var doc = JsonSerializer.Deserialize<FlowDocument>(json, options);
            if (doc is null)
            {
                StatusText.Text = "文件格式无效";
                return false;
            }

            _graph.Clear();

            var nodeMap = new Dictionary<int, Node>();
            foreach (var nd in doc.Nodes)
            {
                Node? node = null;
                try
                {
                    node = NodeFactory.CreateByTypeId(nd.TypeId, nd.X, nd.Y);
                }
                catch (Exception ex)
                {
                    Logger.Warn("Open", $"创建节点失败，跳过：{nd.TypeId} ({nd.DisplayName})，原因：{ex.Message}");
                    continue;
                }
                if (node is null)
                {
                    Logger.Warn("Open", $"跳过未知节点类型：{nd.TypeId} ({nd.DisplayName})");
                    continue;
                }
                node.DisplayName = nd.DisplayName;
                foreach (var (k, v) in nd.Parameters)
                {
                    node.Parameters[k] = v;
                }
                _graph.AddNode(node);
                nodeMap[nd.Idx] = node;
            }

            foreach (var ed in doc.Edges)
            {
                if (!nodeMap.TryGetValue(ed.FromNodeIdx, out var fromNode) ||
                    !nodeMap.TryGetValue(ed.ToNodeIdx, out var toNode))
                {
                    Logger.Warn("Open", $"跳过无效连线：{ed.FromNodeIdx}->{ed.ToNodeIdx}");
                    continue;
                }
                var fromPort = fromNode.FindOutput(ed.FromPort);
                var toPort = toNode.FindInput(ed.ToPort);
                if (fromPort is null || toPort is null)
                {
                    Logger.Warn("Open", $"跳过无效端口：{ed.FromPort} / {ed.ToPort}");
                    continue;
                }
                _graph.TryAddEdge(fromPort, toPort);
            }

            Logger.Success("Open", $"加载流程图 '{path}'：{_graph.Nodes.Count} 节点 + {_graph.Edges.Count} 连线");
            return true;
        }
        catch (Exception ex)
        {
            StatusText.Text = $"加载失败：{ex.Message}";
            Logger.Error("Open", $"加载失败：{ex.Message}");
            return false;
        }
    }

    // ============================================================
    // 保存：把整张 Graph 序列化为 JSON 文件
    //   - 节点：InstanceId / TypeId / DisplayName / AlgoName / DllName / X / Y
    //   - 参数：所有 Parameters 值（含图像源 path、模板 ROI 等）
    //   - 边：From 节点 InstanceId + 端口名 / To 节点 InstanceId + 端口名
    // ============================================================
    private void BtnSave_Click(object sender, RoutedEventArgs e)
    {
        if (_graph.Nodes.Count == 0)
        {
            StatusText.Text = "画布为空，无需保存";
            return;
        }

        var sfd = new SaveFileDialog
        {
            Title = "保存流程图",
            Filter = "VisionFlow 流程图|*.vflow|JSON 文件|*.json|所有文件|*.*",
            FileName = "flow.vflow",
            OverwritePrompt = true,
        };
        if (sfd.ShowDialog() != true) return;

        if (SaveToFile(sfd.FileName))
            StatusText.Text = $"已保存：{sfd.FileName}（{_graph.Nodes.Count} 节点 / {_graph.Edges.Count} 连线）";
    }

    /// <summary>自动保存文件路径（程序目录下 autosave.vflow）</summary>
    private static string AutoSavePath =>
        Path.Combine(AppContext.BaseDirectory, "autosave.vflow");

    /// <summary>序列化当前 Graph 到指定路径。成功返回 true。</summary>
    private bool SaveToFile(string path)
    {
        try
        {
            var dto = new FlowDocument
            {
                Version = 1,
                SavedAt = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"),
            };

            var idMap = new Dictionary<Guid, int>();
            int idx = 0;
            foreach (var n in _graph.Nodes)
            {
                idMap[n.InstanceId] = idx;
                var nodeDto = new NodeDto
                {
                    Idx = idx,
                    TypeId = n.TypeId,
                    DisplayName = n.DisplayName,
                    AlgoName = n.AlgoName,
                    DllName = n.DllName,
                    X = n.X,
                    Y = n.Y,
                    Parameters = n.Parameters.ToDictionary(
                        kv => kv.Key,
                        kv => kv.Value?.ToString() ?? ""),
                };
                dto.Nodes.Add(nodeDto);
                idx++;
            }

            foreach (var ed in _graph.Edges)
            {
                dto.Edges.Add(new EdgeDto
                {
                    FromNodeIdx = idMap[ed.From.Owner.InstanceId],
                    FromPort = ed.From.Name,
                    ToNodeIdx = idMap[ed.To.Owner.InstanceId],
                    ToPort = ed.To.Name,
                });
            }

            var options = new JsonSerializerOptions
            {
                WriteIndented = true,
                Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
            };
            var json = JsonSerializer.Serialize(dto, options);
            File.WriteAllText(path, json);
            Logger.Info("Save", $"保存流程图到 '{path}'：{_graph.Nodes.Count} 节点 + {_graph.Edges.Count} 连线");
            return true;
        }
        catch (Exception ex)
        {
            // 注意：不在此处访问 StatusText，因为 SaveToFile 可能在 Application.Exit
            // 阶段（窗口已关闭）被调用。UI 提示由调用方负责。
            Logger.Error("Save", $"保存失败：{ex.Message}");
            return false;
        }
    }

    // ============================================================
    // 新建（清空画布）
    // ============================================================
    private void BtnNew_Click(object sender, RoutedEventArgs e)
    {
        var n = _graph.Nodes.Count;
        _graph.Clear();
        StatusText.Text = "已清空画布";
        Logger.Info("Canvas", $"清空画布：删除了 {n} 个节点 + {_graph.Edges.Count} 条边");
    }

    // ============================================================
    // 保存 DTO（与 JSON 文件格式对应）
    // ============================================================
    public sealed class FlowDocument
    {
        public int Version { get; set; }
        public string SavedAt { get; set; } = "";
        public List<NodeDto> Nodes { get; set; } = new();
        public List<EdgeDto> Edges { get; set; } = new();
    }

    public sealed class NodeDto
    {
        public int Idx { get; set; }
        public string TypeId { get; set; } = "";
        public string DisplayName { get; set; } = "";
        public string AlgoName { get; set; } = "";
        public string DllName { get; set; } = "";
        public double X { get; set; }
        public double Y { get; set; }
        public Dictionary<string, string> Parameters { get; set; } = new();
    }

    public sealed class EdgeDto
    {
        public int FromNodeIdx { get; set; }
        public string FromPort { get; set; } = "";
        public int ToNodeIdx { get; set; }
        public string ToPort { get; set; } = "";
    }

    /// <summary>解析 ROI 参数字符串中的数值数组。</summary>
    private static double[] ParseRoiDoubles(string s)
    {
        var lst = new System.Collections.Generic.List<double>();
        var body = s.Trim().Trim('[', ']', '"', ' ', '\t');
        foreach (var t in body.Split(new[] { ',', ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
        {
            if (double.TryParse(t, System.Globalization.NumberStyles.Any,
                System.Globalization.CultureInfo.InvariantCulture, out var v))
                lst.Add(v);
        }
        return lst.ToArray();
    }
}
