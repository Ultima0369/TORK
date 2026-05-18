from __future__ import annotations

import json
import os
import time

import requests


class TorkAPI:
    MAX_CONVERSATION: int = 50

    def __init__(
        self,
        api_key: str | None = None,
        config_path: str | None = None,
    ) -> None:
        # 从配置文件加载
        if config_path is None:
            config_path = os.path.join(os.path.dirname(__file__), 'api_config.json')
        
        self.config: dict[str, object] = {}
        if os.path.exists(config_path):
            with open(config_path) as f:
                self.config = json.load(f)
        
        self.api_key: str = api_key or self.config.get('api_key', '') or os.environ.get('DEEPSEEK_API_KEY', '')
        self.base_url: str = self.config.get('base_url', 'https://maas-coding-api.cn-huabei-1.xf-yun.com/v2')
        self.model: str = self.config.get('model', 'astron-code-latest')
        self.temperature: float = self.config.get('temperature', 0.7)
        self.max_tokens: int = self.config.get('max_tokens', 4096)
        self.timeout: int = self.config.get('timeout', 10)  # P0: timeout=10s
        self.max_retries: int = self.config.get('max_retries', 3)  # P0: retry=3
        self._session: requests.Session = requests.Session()
        self._session.headers.update({
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json"
        })
        self.conversation: list[dict[str, str]] = []
        self.system_prompt: str = (
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
    
    def ask(
        self,
        message: str,
        temperature: float | None = None,
        max_tokens: int | None = None,
    ) -> str:
        self.conversation.append({"role": "user", "content": message})
        
        payload: dict[str, object] = {
            "model": self.model,
            "messages": [{"role": "system", "content": self.system_prompt}] + self.conversation[-10:],
            "temperature": temperature if temperature is not None else self.temperature,
            "max_tokens": max_tokens if max_tokens is not None else self.max_tokens,
            "stream": False
        }
        
        for attempt in range(self.max_retries):
            try:
                resp: requests.Response = self._session.post(
                    f"{self.base_url.rstrip('/')}/chat/completions",
                    json=payload,
                    timeout=self.timeout
                )
                if resp.status_code == 200:
                    reply: str = resp.json()['choices'][0]['message']['content']
                    self.conversation.append({"role": "assistant", "content": reply})
                    if len(self.conversation) > self.MAX_CONVERSATION:
                        self.conversation = self.conversation[-self.MAX_CONVERSATION:]
                    return reply
                if resp.status_code >= 500 and attempt < self.max_retries - 1:
                    time.sleep(0.5 * (attempt + 1))
                    continue
                return f"API Error [{resp.status_code}]: {resp.text[:200]}"
            except requests.exceptions.Timeout:
                if attempt < self.max_retries - 1:
                    time.sleep(0.5 * (attempt + 1))
                    continue
                return f"Error: timeout after {self.max_retries} retries"
            except Exception as e:
                if attempt < self.max_retries - 1:
                    time.sleep(0.5 * (attempt + 1))
                    continue
                return f"Error: {str(e)}"
    
    def ask_simple(
        self,
        message: str,
        temperature: float = 0.5,
    ) -> str:
        """无状态的单次请求，不需要对话历史"""
        payload: dict[str, object] = {
            "model": self.model,
            "messages": [
                {"role": "system", "content": "你是 TORK 的云端进化导师。你直接输出工程指令。"},
                {"role": "user", "content": message}
            ],
            "temperature": temperature,
            "max_tokens": self.max_tokens,
            "stream": False
        }
        
        for attempt in range(self.max_retries):
            try:
                resp: requests.Response = self._session.post(
                    f"{self.base_url.rstrip('/')}/chat/completions",
                    json=payload,
                    timeout=self.timeout
                )
                if resp.status_code == 200:
                    return resp.json()['choices'][0]['message']['content']
                if resp.status_code >= 500 and attempt < self.max_retries - 1:
                    time.sleep(0.5 * (attempt + 1))
                    continue
                return f"API Error: {resp.status_code}"
            except requests.exceptions.Timeout:
                if attempt < self.max_retries - 1:
                    time.sleep(0.5 * (attempt + 1))
                    continue
                return f"Error: timeout after {self.max_retries} retries"
            except Exception as e:
                if attempt < self.max_retries - 1:
                    time.sleep(0.5 * (attempt + 1))
                    continue
                return f"Error: {str(e)}"
    
    def save(self, path: str | None = None) -> None:
        if path is None:
            path = os.path.join(os.path.dirname(__file__), 'conversation.json')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            json.dump(self.conversation, f, ensure_ascii=False, indent=2)
    
    def load(self, path: str | None = None) -> None:
        if path is None:
            path = os.path.join(os.path.dirname(__file__), 'conversation.json')
        if os.path.exists(path):
            with open(path, 'r') as f:
                self.conversation = json.load(f)


def test_connection() -> str:
    """测试 API 连接"""
    api = TorkAPI()
    result: str = api.ask_simple("Respond with exactly: TORK Cloud Brain Connected. Model: " + api.model)
    print(f"🔌 {result}")
    return result


if __name__ == "__main__":
    test_connection()
