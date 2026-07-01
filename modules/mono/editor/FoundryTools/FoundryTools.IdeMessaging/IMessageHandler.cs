using System.Threading.Tasks;

namespace FoundryTools.IdeMessaging
{
    public interface IMessageHandler
    {
        public Task<MessageContent> HandleRequest(Peer peer, string id, MessageContent content, ILogger logger);
    }
}
