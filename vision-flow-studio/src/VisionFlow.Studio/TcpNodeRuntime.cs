using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace VisionFlow.Studio;

/// <summary>
/// TCP 节点运行时：管理 TcpServer（监听）与 TcpClient（连接）的长连接状态，
/// 按“结束符”把收到的字节流拆成完整消息，供 DagEngine 在每次流程迭代时取出。
/// 线程安全；监听/收发均在后台 Task，不阻塞 UI。
/// </summary>
public static class TcpNodeRuntime
{
    private sealed class Session : IDisposable
    {
        public CancellationTokenSource Cts = new();
        public TcpListener? Listener;
        public TcpClient? Client;
        public readonly ConcurrentQueue<string> Messages = new();
        public volatile bool Listening;
        public volatile bool Connected;
        public string Terminator = "\n";
        public string LastError = "";
        public string LastMessage = "";   // 最近一次收到的完整消息（供输出端口稳定显示）
        public void Dispose()
        {
            try { Cts.Cancel(); } catch { }
            try { Client?.Close(); } catch { }
            try { Listener?.Stop(); } catch { }
            try { Cts.Dispose(); } catch { }
        }
    }

    private static readonly ConcurrentDictionary<Guid, Session> _sessions = new();

    private static Session GetOrAdd(Guid id) => _sessions.GetOrAdd(id, _ => new Session());

    // ---------------- 状态查询 ----------------
    public static bool IsListening(Guid id) => _sessions.TryGetValue(id, out var s) && s.Listening;
    public static bool IsConnected(Guid id) => _sessions.TryGetValue(id, out var s) && s.Connected;
    public static string LastError(Guid id) => _sessions.TryGetValue(id, out var s) ? s.LastError : "";

    // ---------------- 服务端 ----------------
    public static void StartServer(Guid id, string ip, int port, string terminator)
    {
        Stop(id);
        var s = GetOrAdd(id);
        s.Terminator = string.IsNullOrEmpty(terminator) ? "\n" : terminator;
        s.LastError = "";
        try
        {
            var addr = string.IsNullOrWhiteSpace(ip) || ip == "0.0.0.0" || ip == "*"
                ? IPAddress.Any : IPAddress.Parse(ip.Trim());
            s.Listener = new TcpListener(addr, port);
            s.Listener.Start();
            s.Listening = true;
            _ = Task.Run(() => AcceptLoopAsync(s, s.Cts.Token));
        }
        catch (Exception ex)
        {
            s.LastError = ex.Message;
            s.Listening = false;
        }
    }

    private static async Task AcceptLoopAsync(Session s, CancellationToken ct)
    {
        try
        {
            while (!ct.IsCancellationRequested && s.Listener is not null)
            {
                var client = await s.Listener.AcceptTcpClientAsync(ct).ConfigureAwait(false);
                _ = Task.Run(() => HandleClientAsync(s, client, ct), ct);
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { s.LastError = ex.Message; }
        finally { s.Listening = false; }
    }

    private static async Task HandleClientAsync(Session s, TcpClient client, CancellationToken ct)
    {
        try
        {
            using (client)
            using (var stream = client.GetStream())
            {
                var buf = new byte[4096];
                var sb = new StringBuilder();
                while (!ct.IsCancellationRequested && client.Connected)
                {
                    int n = await stream.ReadAsync(buf.AsMemory(0, buf.Length), ct).ConfigureAwait(false);
                    if (n <= 0) break;
                    sb.Append(Encoding.UTF8.GetString(buf, 0, n));
                    ExtractMessages(s, sb);
                }
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { s.LastError = ex.Message; }
    }

    // ---------------- 客户端 ----------------
    public static void ConnectClient(Guid id, string ip, int port, string trigger, string terminator)
    {
        Stop(id);
        var s = GetOrAdd(id);
        s.Terminator = string.IsNullOrEmpty(terminator) ? "\n" : terminator;
        s.LastError = "";
        try
        {
            s.Client = new TcpClient();
            _ = ConnectClientAsync(s, ip.Trim(), port, trigger, s.Cts.Token);
        }
        catch (Exception ex) { s.LastError = ex.Message; }
    }

    private static async Task ConnectClientAsync(Session s, string ip, int port, string trigger, CancellationToken ct)
    {
        try
        {
            await s.Client!.ConnectAsync(ip, port, ct).ConfigureAwait(false);
            s.Connected = true;
            var stream = s.Client.GetStream();
            if (!string.IsNullOrEmpty(trigger))
            {
                var bytes = Encoding.UTF8.GetBytes(trigger);
                await stream.WriteAsync(bytes.AsMemory(), ct).ConfigureAwait(false);
            }
            var buf = new byte[4096];
            var sb = new StringBuilder();
            while (!ct.IsCancellationRequested && s.Client.Connected)
            {
                int n = await stream.ReadAsync(buf.AsMemory(0, buf.Length), ct).ConfigureAwait(false);
                if (n <= 0) break;
                sb.Append(Encoding.UTF8.GetString(buf, 0, n));
                ExtractMessages(s, sb);
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { s.LastError = ex.Message; }
        finally { s.Connected = false; }
    }

    public static void SendClient(Guid id, string text)
    {
        if (!_sessions.TryGetValue(id, out var s) || s.Client is null || !s.Connected) return;
        if (string.IsNullOrEmpty(text)) return;
        try
        {
            var bytes = Encoding.UTF8.GetBytes(text);
            _ = s.Client.GetStream().WriteAsync(bytes.AsMemory(), s.Cts.Token);
        }
        catch (Exception ex) { s.LastError = ex.Message; }
    }

    // ---------------- 拆包 + 取消息 ----------------
    private static void ExtractMessages(Session s, StringBuilder sb)
    {
        string term = s.Terminator;
        int idx;
        while ((idx = sb.ToString().IndexOf(term, StringComparison.Ordinal)) >= 0)
        {
            var msg = sb.ToString(0, idx);
            sb.Remove(0, idx + term.Length);
            if (msg.Length > 0)
            {
                s.Messages.Enqueue(msg);
                s.LastMessage = msg;
            }
        }
    }

    /// <summary>返回最近一次收到的完整消息（无则空串）。用于输出端口稳定展示“接收到的内容”。</summary>
    public static string LatestMessage(Guid id)
        => _sessions.TryGetValue(id, out var s) ? s.LastMessage : "";

    /// <summary>取出一条已收到的完整消息（FIFO）；无则返回空串。每次流程迭代调用以驱动数据。</summary>
    public static string TakeOne(Guid id)
    {
        if (_sessions.TryGetValue(id, out var s) && s.Messages.TryDequeue(out var msg))
            return msg;
        return "";
    }

    public static int PendingCount(Guid id)
        => _sessions.TryGetValue(id, out var s) ? s.Messages.Count : 0;

    public static void Stop(Guid id)
    {
        if (_sessions.TryRemove(id, out var s))
            s.Dispose();
    }

    public static void StopAll()
    {
        foreach (var id in _sessions.Keys.ToList()) Stop(id);
    }
}
