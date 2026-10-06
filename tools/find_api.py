import re

data = open(r"E:/Programs/WorkBuddy/resources/app.asar", "rb").read()
hosts = set()
for m in re.finditer(rb"https?://[a-zA-Z0-9._\-]+", data):
    u = m.group().decode("ascii", "ignore")
    if re.search(r"codebuddy|copilot|tencent|cloud.tencent", u, re.I) and "docs\|doc\|img\|gtimg" not in u:
        hosts.add(u)
for h in sorted(hosts):
    print(h)
