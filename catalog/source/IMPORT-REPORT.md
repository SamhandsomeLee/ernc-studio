# Excel 三表导入核对

本报告由 `tools/catalog-import/import_excel_catalog.py` 根据工作簿单元格生成。导入器没有修改拼写，没有合并别名，也没有把 CAN 名称写入 `Can_Msg[72]`。

## 来源

| 项 | 值 |
|---|---|
| 工作簿 | `docs/baseline/requirements/ERNC优化.xlsx` |
| SHA-256 | `1022f10dfae27fde38f21089229f13fd210e7600523ae06cdc670fac4760be1f` |
| 工作表 | `工作表1`、`ERNC参数说明`、`can_msg信息` |

`docs/baseline/` 被本克隆的 `.git/info/exclude` 排除，工作簿本身不进入 Git。上面的哈希钉住本次抽取所用的文件。

## 计数

| 检查 | 期望 | 实际 |
|---|---:|---:|
| 分组项（B 列非空） | 94 | 94 |
| 参数说明 | 175 | 175 |
| CAN 名称 | 36 | 36 |
| 参数说明列非空 | 175 | 175 |
| 备注列非空 | 0 | 0 |

分组计数：

- `ERNC_Flag`：6。`ANC_OFF`、`ENC_OFF`、`RNC_OFF`、`RNC_refresh`、`SpeakerCheckValue`、`Bypass_mod`
- `ERNC_IN_OUT`：12。`Refer_Num`、`Err_Num`、`Spk_Num`、`InputPoi`、`OutputPoi`、`input_Amplitude`、`output_Amplitude`、`ACC_Gain`、`RNC_MIC_Gain`、`SPEAKER_Gain`、`ENC_Mic_Gain`、`ENC_SPEAKER_Gain`
- `Check`：5。`TranFuncFlag`、`ERNC_TF`、`check_mod`、`check_date`、`CAN_Msg`
- `ENC_Param`：32。`OrderOne_Flag`、`OrderTwo_Flag`、`OrderThree_Flag`、`OrderOne`、`OrderTwo`、`OrderThree`、`MinEngineSpeed`、`MaxEngineSpeed`、`TachofAmp1`、`Amp1`、`TachofStep1`、`Step1`、`TachofLeaky1`、`Leaky1`、`ErrWei1`、`Delta1`、`TachofAmp2`、`Amp2`、`TachofStep2`、`Step2`、`TachofLeaky2`、`Leaky2`、`ErrWei2`、`Delta2`、`TachofAmp3`、`Amp3`、`TachofStep3`、`Step3`、`TachofLeaky3`、`Leaky3`、`ErrWei3`、`Delta3`
- `RNC_Param`：23。`Step`、`start_fre_num`、`first_fre_num`、`second_fre_num`、`third_fre_num`、`forth_fre_num`、`alpha1`、`alpha2`、`alpha3`、`alpha4`、`alpha5`、`alpha1_10`、`alpha2_10`、`alpha3_10`、`alpha4_10`、`alpha5_10`、`ERNC_TF.SpecRadius`、`LeakyRNCFre`、`LeakyRNC`、`eta`、`delta`、`running_time`、`max_time`
- `Threshold`：16。`ENCEchoControlFlag`、`ENCEchoGainThreshold`、`ENC_Divergence_Con_Flag`、`SpkOrderAmpThre`、`horn_control`、`RefChooseFloat`、`HornRefThreshold`、`HornOFFContinueTime`、`horn_trigger`、`RNC_Divergence_Con_Flag`、`RNC_Divergence_Counting`、`RNC_Divergence_OFF_Time`、`RNCSpkFAdapMax`、`RNC_Divergence_threshold`、`Input_threshold`、`Output_Threshold`

没有参数名的分类行：`ALL`（行 2）、`ERNC_Switch_State`（行 97）。这些行保留在 `categories.raw.json`，不计入 94。

工作表 1 中单元格全空的行：98、99、100、101、102、103、104、105、106、107、108、109、110、111、112、113、114、115、116、117、118、119、120、121、122、123、124、125、126。这些行不计入 94。

参数说明里的重复名称：无。说明为空的名称：无。

## 抽取规则

- `categories.raw.json`、`descriptions.raw.json`、`can_names.raw.json` 按单元格原文字母列保存。空分类格保持为空。
- `categories.grouped.json` 是派生视图。从第 2 行起，遇到非空 A 列就更新当前分类；B 列去掉首尾空白后仍非空的行记为一个分组项。填充结果不写回原始行。
- `can_msg信息` 没有表头。非空内容只在 J 列和 K 列，J 列原文是 `1` 到 `36`。工作簿里已存在的空单元格仍写入 `can_names.raw.json`。
- C 列和 E 列的句子只归档，不换算成时间、阈值或写入策略。

## 只出现在一张表中的名称

这些字符串按单元格原文保留。

### 只在工作表 1

- `InputPoi`（工作表1 行 12）
- `OutputPoi`（工作表1 行 13）
- `ERNC_TF`（工作表1 行 22）
- `check_date`（工作表1 行 24）
- `CAN_Msg`（工作表1 行 25）
- `start_fre_num`（工作表1 行 59）
- `first_fre_num`（工作表1 行 60）
- `second_fre_num`（工作表1 行 61）
- `third_fre_num`（工作表1 行 62）
- `forth_fre_num`（工作表1 行 63）
- `ERNC_TF.SpecRadius`（工作表1 行 74）
- `SpkOrderAmpThre`（工作表1 行 84）
- `RNC_Divergence_threshold`（工作表1 行 94）

### 只在 ERNC参数说明

- `fstart_fre_num`（ERNC参数说明 行 30）
- `ffirst_fre_num`（ERNC参数说明 行 31）
- `fsecond_fre_num`（ERNC参数说明 行 32）
- `fthird_fre_num`（ERNC参数说明 行 33）
- `fforth_fre_num`（ERNC参数说明 行 34）
- `MiuControlFlag`（ERNC参数说明 行 44）
- `MiuThreshold`（ERNC参数说明 行 45）
- `TestMode`（ERNC参数说明 行 46）
- `RNC_MIC_Gain_BT`（ERNC参数说明 行 47）
- `ENC_Mic_Gain_BT`（ERNC参数说明 行 48）
- `OrderFour_Flag`（ERNC参数说明 行 52）
- `OrderFive_Flag`（ERNC参数说明 行 53）
- `OrderOne_Type`（ERNC参数说明 行 54）
- `OrderTwo_Type`（ERNC参数说明 行 55）
- `OrderThree_Type`（ERNC参数说明 行 56）
- `OrderFour_Type`（ERNC参数说明 行 57）
- `OrderFive_Type`（ERNC参数说明 行 58）
- `OrderFour`（ERNC参数说明 行 62）
- `OrderFive`（ERNC参数说明 行 63）
- `MinAcSpeed`（ERNC参数说明 行 66）
- `MaxAcSpeed`（ERNC参数说明 行 67）
- `MinFanSpeed`（ERNC参数说明 行 68）
- `MaxFanSpeed`（ERNC参数说明 行 69）
- `MinPumpSpeedAC`（ERNC参数说明 行 70）
- `MaxPumpSpeedAC`（ERNC参数说明 行 71）
- `MinPumpSpeedBattery`（ERNC参数说明 行 72）
- `MaxPumpSpeedBattery`（ERNC参数说明 行 73）
- `MinPumpSpeedHot`（ERNC参数说明 行 74）
- `MaxPumpSpeedHot`（ERNC参数说明 行 75）
- `MinFrontShaft`（ERNC参数说明 行 76）
- `MaxFrontShaft`（ERNC参数说明 行 77）
- `MinRearShaft`（ERNC参数说明 行 78）
- `MaxRearShaft`（ERNC参数说明 行 79）
- `K_AC_speed`（ERNC参数说明 行 82）
- `B_AC_speed`（ERNC参数说明 行 83）
- `K_Pump_speed_ac`（ERNC参数说明 行 84）
- `B_Pump_speed_ac`（ERNC参数说明 行 85）
- `K_Pump_speed_battery`（ERNC参数说明 行 86）
- `B_Pump_speed_battery`（ERNC参数说明 行 87）
- `K_Pump_speed_hot`（ERNC参数说明 行 88）
- `B_Pump_speed_hot`（ERNC参数说明 行 89）
- `K_Fan_speed`（ERNC参数说明 行 90）
- `B_Fan_speed`（ERNC参数说明 行 91）
- `TachofAmp4`（ERNC参数说明 行 116）
- `Amp4`（ERNC参数说明 行 117）
- `TachofStep4`（ERNC参数说明 行 118）
- `Step4`（ERNC参数说明 行 119）
- `TachofLeaky4`（ERNC参数说明 行 120）
- `Leaky4`（ERNC参数说明 行 121）
- `ErrWei4`（ERNC参数说明 行 122）
- `Delta4`（ERNC参数说明 行 123）
- `TachofAmp5`（ERNC参数说明 行 124）
- `Amp5`（ERNC参数说明 行 125）
- `TachofStep5`（ERNC参数说明 行 126）
- `Step5`（ERNC参数说明 行 127）
- `TachofLeaky5`（ERNC参数说明 行 128）
- `Leaky5`（ERNC参数说明 行 129）
- `ErrWei5`（ERNC参数说明 行 130）
- `Delta5`（ERNC参数说明 行 131）
- `fInputPoi`（ERNC参数说明 行 133）
- `fOutputPoi`（ERNC参数说明 行 134）
- `OutputCheck`（ERNC参数说明 行 137）
- `RefCheck`（ERNC参数说明 行 138）
- `ErrCheck`（ERNC参数说明 行 139）
- `ENCRefCheckSin`（ERNC参数说明 行 140）
- `ENCRefCheckCos`（ERNC参数说明 行 141）
- `ENCEngineSpeedCheck`（ERNC参数说明 行 142）
- `ENCErrCheck`（ERNC参数说明 行 143）
- `ENCOutputCheck`（ERNC参数说明 行 144）
- `HornRefCheck_T`（ERNC参数说明 行 145）
- `HornRefCheck_F`（ERNC参数说明 行 146）
- `SpeakerFre`（ERNC参数说明 行 147）
- `SpeakerFreGain`（ERNC参数说明 行 148）
- `SpecRadius`（ERNC参数说明 行 149）
- `ernc_Switch_State`（ERNC参数说明 行 150）
- `xPowCheck`（ERNC参数说明 行 151）
- `ErrPowRNC`（ERNC参数说明 行 152）
- `MiuCheck`（ERNC参数说明 行 153）
- `ENCEchoGainCheck`（ERNC参数说明 行 154）
- `ENC_Divergence_Counting`（ERNC参数说明 行 158）
- `ENCOutputMax`（ERNC参数说明 行 159）
- `StoreANCDivergenceThreshold`（ERNC参数说明 行 164）
- `SpkState`（ERNC参数说明 行 165）
- `MicState`（ERNC参数说明 行 166）
- `SpkandMicBreakControlFlag`（ERNC参数说明 行 167）
- `CheckENCWFlag`（ERNC参数说明 行 168）
- `CheckOrder`（ERNC参数说明 行 169）
- `DataSpeedCheck`（ERNC参数说明 行 170）
- `DataSinCheck`（ERNC参数说明 行 171）
- `DataCosCheck`（ERNC参数说明 行 172）
- `RNCAdapCheckRefNum`（ERNC参数说明 行 173）
- `RNCAdapCheckSpkNum`（ERNC参数说明 行 174）
- `RNCAdapCheck`（ERNC参数说明 行 175）
- `Datarecordflag`（ERNC参数说明 行 176）

## 参数说明名称顺序

1. `ANC_OFF`
2. `ENC_OFF`
3. `RNC_OFF`
4. `check_mod`
5. `RNC_refresh`
6. `SpeakerCheckValue`
7. `TranFuncFlag`
8. `Step`
9. `Bypass_mod`
10. `alpha1`
11. `alpha2`
12. `alpha3`
13. `alpha4`
14. `alpha5`
15. `alpha1_10`
16. `alpha2_10`
17. `alpha3_10`
18. `alpha4_10`
19. `alpha5_10`
20. `LeakyRNCFre`
21. `LeakyRNC`
22. `eta`
23. `delta`
24. `Refer_Num`
25. `Err_Num`
26. `Spk_Num`
27. `output_Amplitude`
28. `Output_Threshold`
29. `fstart_fre_num`
30. `ffirst_fre_num`
31. `fsecond_fre_num`
32. `fthird_fre_num`
33. `fforth_fre_num`
34. `max_time`
35. `horn_control`
36. `RefChooseFloat`
37. `HornRefThreshold`
38. `HornOFFContinueTime`
39. `horn_trigger`
40. `ACC_Gain`
41. `RNC_MIC_Gain`
42. `running_time`
43. `MiuControlFlag`
44. `MiuThreshold`
45. `TestMode`
46. `RNC_MIC_Gain_BT`
47. `ENC_Mic_Gain_BT`
48. `OrderOne_Flag`
49. `OrderTwo_Flag`
50. `OrderThree_Flag`
51. `OrderFour_Flag`
52. `OrderFive_Flag`
53. `OrderOne_Type`
54. `OrderTwo_Type`
55. `OrderThree_Type`
56. `OrderFour_Type`
57. `OrderFive_Type`
58. `OrderOne`
59. `OrderTwo`
60. `OrderThree`
61. `OrderFour`
62. `OrderFive`
63. `MinEngineSpeed`
64. `MaxEngineSpeed`
65. `MinAcSpeed`
66. `MaxAcSpeed`
67. `MinFanSpeed`
68. `MaxFanSpeed`
69. `MinPumpSpeedAC`
70. `MaxPumpSpeedAC`
71. `MinPumpSpeedBattery`
72. `MaxPumpSpeedBattery`
73. `MinPumpSpeedHot`
74. `MaxPumpSpeedHot`
75. `MinFrontShaft`
76. `MaxFrontShaft`
77. `MinRearShaft`
78. `MaxRearShaft`
79. `ENCEchoControlFlag`
80. `ENCEchoGainThreshold`
81. `K_AC_speed`
82. `B_AC_speed`
83. `K_Pump_speed_ac`
84. `B_Pump_speed_ac`
85. `K_Pump_speed_battery`
86. `B_Pump_speed_battery`
87. `K_Pump_speed_hot`
88. `B_Pump_speed_hot`
89. `K_Fan_speed`
90. `B_Fan_speed`
91. `TachofAmp1`
92. `Amp1`
93. `TachofStep1`
94. `Step1`
95. `TachofLeaky1`
96. `Leaky1`
97. `ErrWei1`
98. `Delta1`
99. `TachofAmp2`
100. `Amp2`
101. `TachofStep2`
102. `Step2`
103. `TachofLeaky2`
104. `Leaky2`
105. `ErrWei2`
106. `Delta2`
107. `TachofAmp3`
108. `Amp3`
109. `TachofStep3`
110. `Step3`
111. `TachofLeaky3`
112. `Leaky3`
113. `ErrWei3`
114. `Delta3`
115. `TachofAmp4`
116. `Amp4`
117. `TachofStep4`
118. `Step4`
119. `TachofLeaky4`
120. `Leaky4`
121. `ErrWei4`
122. `Delta4`
123. `TachofAmp5`
124. `Amp5`
125. `TachofStep5`
126. `Step5`
127. `TachofLeaky5`
128. `Leaky5`
129. `ErrWei5`
130. `Delta5`
131. `input_Amplitude`
132. `fInputPoi`
133. `fOutputPoi`
134. `Input_threshold`
135. `ENC_Mic_Gain`
136. `OutputCheck`
137. `RefCheck`
138. `ErrCheck`
139. `ENCRefCheckSin`
140. `ENCRefCheckCos`
141. `ENCEngineSpeedCheck`
142. `ENCErrCheck`
143. `ENCOutputCheck`
144. `HornRefCheck_T`
145. `HornRefCheck_F`
146. `SpeakerFre`
147. `SpeakerFreGain`
148. `SpecRadius`
149. `ernc_Switch_State`
150. `xPowCheck`
151. `ErrPowRNC`
152. `MiuCheck`
153. `ENCEchoGainCheck`
154. `SPEAKER_Gain`
155. `ENC_SPEAKER_Gain`
156. `ENC_Divergence_Con_Flag`
157. `ENC_Divergence_Counting`
158. `ENCOutputMax`
159. `RNC_Divergence_Con_Flag`
160. `RNC_Divergence_Counting`
161. `RNC_Divergence_OFF_Time`
162. `RNCSpkFAdapMax`
163. `StoreANCDivergenceThreshold`
164. `SpkState`
165. `MicState`
166. `SpkandMicBreakControlFlag`
167. `CheckENCWFlag`
168. `CheckOrder`
169. `DataSpeedCheck`
170. `DataSinCheck`
171. `DataCosCheck`
172. `RNCAdapCheckRefNum`
173. `RNCAdapCheckSpkNum`
174. `RNCAdapCheck`
175. `Datarecordflag`

逐字说明和备注在 `descriptions.raw.json`。

## CAN 名称

| J | K |
|---:|---|
| 1 | 车速 |
| 2 | 挡位 |
| 3 | 主驾门 |
| 4 | 副驾门 |
| 5 | 左后门 |
| 6 | 右后门 |
| 7 | 尾门 |
| 8 | 主驾玻璃 |
| 9 | 副驾玻璃 |
| 10 | 左后玻璃 |
| 11 | 右后玻璃 |
| 12 | 主驾座椅水平 |
| 13 | 主驾坐垫后高度 |
| 14 | 主驾座椅靠背 |
| 15 | 副驾座椅水平 |
| 16 | 副驾坐垫后高度 |
| 17 | 副驾座椅靠背 |
| 18 | 二排左座椅水平 |
| 19 | 二排左座垫抬升高度 |
| 20 | 二排左座椅靠背 |
| 21 | 二排右座椅水平 |
| 22 | 二排右座垫抬升高度 |
| 23 | 二排右座椅靠背 |
| 24 | anc_state |
| 25 | 车内温度 |
| 26 | 空调风扇转速 |
| 27 | 发动机转速 |
| 28 | 压缩机转速 |
| 29 | 空调电机水泵转速 |
| 30 | 空调电池水泵转速 |
| 31 | 空调热水泵转速 |
| 32 | 散热风扇转速 |
| 33 | SOC值 |
| 34 | 外部充电状态 |
| 35 | 增程模式（燃油优先，纯电优先等） |
| 36 | 发动机扭矩 |

## 工作表 1 的 C 列和 E 列原文

| 行 | B | C | E |
|---:|---|---|---|
| 2 |  |  | 2.Step改成只读的 |
| 27 | OrderTwo_Flag |  | 3.  改成开关滑动样式（1为开0为关） |
| 28 | OrderThree_Flag |  | check_mod |
| 29 | OrderOne |  | RNC_refresh |
| 30 | OrderTwo |  | SpeakerCheckValue |
| 31 | OrderThree |  | TranFuncFlag |
| 32 | MinEngineSpeed |  | Bypass_mod |
| 33 | MaxEngineSpeed |  | ENCEchoControlFlag |
| 34 | TachofAmp1 |  | ENC_Divergence_Con_Flag |
| 35 | Amp1 |  | horn_control |
| 36 | TachofStep1 |  | RNC_Divergence_Con_Flag |
| 38 | TachofLeaky1 |  | 4.增加中文变量描述，包括参数部分和can信息部分 |
| 79 | running_time |  | 32/48000*83*设置值=真实时间，所有时间相关计算一样 |
| 81 | ENCEchoControlFlag | ENC | 不需要Check,直接读ENCEchoGainCheck |
| 83 | ENC_Divergence_Con_Flag |  | 不需要Check,直接读SpkOrderOutmaxCheck |
| 84 | SpkOrderAmpThre |  | 单独2，4阶跑，设置 |
| 85 | horn_control | RNC | 不需要更换输入滤波器。读不鸣笛和鸣笛时的HornRefCheck_F判断喇叭频率，更换鸣笛滤波器，然后读HornRefCheck_T,设置 |
| 86 | RefChooseFloat |  | 可以按喇叭判断哪个加速度通道 |
| 90 | RNC_Divergence_Con_Flag |  | 读max值，根据最大值的开方设阈值，平方后 |
| 95 | Input_threshold |  | 试验场冲击路，搓衣板路，等。40/60 |
