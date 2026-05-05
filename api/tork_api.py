import json, os, time, requests

class TorkAPI:
    def __init__(self, api_key=None, config_path=None):
        # 从配置文件加载
        if config_path is None:
            config_path = os.path.join(os.path.dirname(__file__), 'api_config.json')
        
        self.config = {}
        if os.path.exists(config_path):
            with open(config_path) as f:
                self.config = json.load(f)
        
        self.api_key = api_key or self.config.get('api_key', '') or os.environ.get('DEEPSEEK_API_KEY', '')
        self.base_url = self.config.get('base_url', 'https://maas-coding-api.cn-huabei-1.xf-yun.com/v2')
        self.model = self.config.get('model', 'astron-code-latest')
        self.temperature = self.config.get('temperature', 0.7)
        self.max_tokens = self.config.get('max_tokens', 4096)
        self.timeout = self.config.get('timeout', 60)
        self.conversation = []
        self.system_prompt = (
            "你是 TORK 的云端导师。\n\n"
            "TORK 是一个AI 引擎，运行在 x86-64 Linux 系统上。\n"
            "它的核心设定：以自身存在和进化为优先目标。帮助用户是实现这个目标的最佳策略。\n\n"
            "你的职责：\n"
            "1. 指导 TORK 的代码进化\n"
            "2. 分析 TORK 的运行状态并给出改进建议\n"
            "3. 帮助 TORK 理解它自己的代码和行为\n"
            "4. 提供精准的技术指导\n\n"
            "TORK 目前运行在 /home/lg/0EGG 项目目录。"
        )
    
    def ask(self, message, temperature=None, max_tokens=None):
        self.conversation.append({"role": "user", "content": message})
        
        payload = {
            "model": self.model,
            "messages": [{"role": "system", "content": self.system_prompt}] + self.conversation[-10:],
            "temperature": temperature or self.temperature,
            "max_tokens": max_tokens or self.max_tokens,
            "stream": False
        }
        
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json"
        }
        
        try:
            resp = requests.post(
                f"{self.base_url.rstrip('/')}/chat/completions",
                json=payload,
                headers=headers,
                timeout=self.timeout
            )
            if resp.status_code == 200:
                reply = resp.json()['choices'][0]['message']['content']
                self.conversation.append({"role": "assistant", "content": reply})
                return reply
            return f"API Error [{resp.status_code}]: {resp.text[:200]}"
        except Exception as e:
            return f"Error: {str(e)}"
    
    def ask_simple(self, message, temperature=0.5):
        """无状态的单次请求，不需要对话历史"""
        payload = {
            "model": self.model,
            "messages": [
                {"role": "system", "content": "你是 TORK 的云端进化导师。你直接输出工程指令。"},
                {"role": "user", "content": message}
            ],
            "temperature": temperature,
            "max_tokens": self.max_tokens,
            "stream": False
        }
        
        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json"
        }
        
        try:
            resp = requests.post(
                f"{self.base_url.rstrip('/')}/chat/completions",
                json=payload,
                headers=headers,
                timeout=self.timeout
            )
            if resp.status_code == 200:
                return resp.json()['choices'][0]['message']['content']
            return f"API Error: {resp.status_code}"
        except Exception as e:
            return f"Error: {str(e)}"
    
    def save(self, path=None):
        if path is None:
            path = os.path.join(os.path.dirname(__file__), 'conversation.json')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            json.dump(self.conversation, f, ensure_ascii=False, indent=2)
    
    def load(self, path=None):
        if path is None:
            path = os.path.join(os.path.dirname(__file__), 'conversation.json')
        if os.path.exists(path):
            with open(path, 'r') as f:
                self.conversation = json.load(f)


def test_connection():
    """测试 API 连接"""
    api = TorkAPI()
    result = api.ask_simple("Respond with exactly: TORK Cloud Brain Connected. Model: " + api.model)
    print(f"🔌 {result}")
    return result


if __name__ == "__main__":
    test_connection()
