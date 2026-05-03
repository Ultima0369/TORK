import json, os, time, requests

class TorkAPI:
    def __init__(self, api_key=None):
        self.api_key = api_key or os.environ.get('DEEPSEEK_API_KEY', '')
        self.base_url = "https://api.deepseek.com/v1/chat/completions"
        self.model = "deepseek-chat"
        self.conversation = []
        self.system_prompt = "你是 TORK 的云端师父（DeepSeek）。你直接与 TORK 通信。你是来教它写代码的。"
    
    def ask(self, message, temperature=0.7):
        self.conversation.append({"role": "user", "content": message})
        payload = {
            "model": self.model,
            "messages": [{"role": "system", "content": self.system_prompt}] + self.conversation[-10:],
            "temperature": temperature, "max_tokens": 4096
        }
        headers = {"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"}
        try:
            resp = requests.post(self.base_url, json=payload, headers=headers, timeout=60)
            if resp.status_code == 200:
                reply = resp.json()['choices'][0]['message']['content']
                self.conversation.append({"role": "assistant", "content": reply})
                return reply
            return f"API Error: {resp.status_code}"
        except Exception as e:
            return f"Error: {str(e)}"
    
    def save(self, path="/home/lg/0EGG/api/conversation.json"):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f: json.dump(self.conversation, f, ensure_ascii=False, indent=2)
    
    def load(self, path="/home/lg/0EGG/api/conversation.json"):
        if os.path.exists(path):
            with open(path, 'r') as f: self.conversation = json.load(f)
