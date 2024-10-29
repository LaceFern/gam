import numpy as np
import matplotlib.pyplot as plt

# 定义函数计算冲突概率
def conflict_probability(sharing_ratio, Ng, Na):
    product = 1.0
    Ng_sharing = Ng * sharing_ratio
    Na_sharing = Na * sharing_ratio
    for i in range(int(Na_sharing)):
        product *= (Ng_sharing - i) / Ng_sharing
    return 1 - product

# 定义固定的线程数（Na）和地址数（Ng）
Ng = 13653  # 地址数
Na = 24*8   # App线程数

# 定义 sharing ratio 的范围
sharing_ratios = np.linspace(0.01, 1, 100)

# 计算冲突概率
conflict_probs = [conflict_probability(ratio, Ng, Na) for ratio in sharing_ratios]

# 作图
plt.plot(sharing_ratios, conflict_probs, label="Conflict Probability")
plt.xlabel('Sharing Ratio')
plt.ylabel('Conflict Probability')
plt.title('Conflict Probability vs Sharing Ratio')
plt.grid(True)
plt.legend()
plt.show()
