package com.tyranor.runtime.systemnnn;

import android.content.SharedPreferences;
import java.io.*;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Map;

/** The production configuration reader runs without graphics/JNI calls. */
public final class GameConfigTest {
    private static int checks;
    private static void check(boolean b,String message){if(!b)throw new AssertionError(message);checks++;}
    private static void write(File file,String text,boolean encrypted)throws Exception{
        file.getParentFile().mkdirs();byte[] bytes=text.getBytes(StandardCharsets.ISO_8859_1);
        if(encrypted)for(int i=0;i<bytes.length;i++)bytes[i]^=(byte)255;
        Files.write(file.toPath(),bytes);
    }
    private static void remove(File file){if(file.isDirectory())for(File child:file.listFiles())remove(child);file.delete();}
    private static byte[] originalValue(File root,String field)throws Exception{
        File file=new File(root,"init2/config.fxf");boolean encrypted=file.isFile();
        if(!encrypted)file=new File(root,"init/config.xtx");
        byte[] input=Files.readAllBytes(file.toPath());if(encrypted)for(int i=0;i<input.length;i++)input[i]^=(byte)255;
        byte[] key=field.getBytes(StandardCharsets.US_ASCII);
        for(int start=0;start<input.length;){
            int end=start;while(end<input.length&&input[end]!=10&&input[end]!=13)end++;
            int first=start,last=end;while(first<last&&(input[first]==32||input[first]==9))first++;
            while(last>first&&(input[last-1]==32||input[last-1]==9))last--;
            if(last-first>key.length){
                boolean match=true;for(int i=0;i<key.length;i++)if(input[first+i]!=key[i])match=false;
                int value=first+key.length;
                if(match&&(input[value]==32||input[value]==9)){
                    while(value<last&&(input[value]==32||input[value]==9))value++;
                    return Arrays.copyOfRange(input,value,last);
                }
            }
            start=end+1;
        }
        throw new AssertionError("Missing original value "+field);
    }
    private static byte[] hex(String value){
        byte[] bytes=new byte[value.length()/2];
        for(int i=0;i<bytes.length;i++)bytes[i]=(byte)Integer.parseInt(value.substring(i*2,i*2+2),16);
        return bytes;
    }
    // Independently fixed original bytes and complete visible text. These do not
    // derive the expected text by invoking either decoder profile.
    private static final String[][] PREVIEWS={
        {"windowMessage1","81a18db68fe382cc825282c282cc835e834f82c594778c69835483938376838b82f082ab82e882a982a682e782ea82dc82b78142","■左上の３つのタグで背景サンプルをきりかえられます。"},
        {"nameWindowMessage","83818362835a815b835783458343839383688345","メッセ—ジウインドウ"},
        {"sampleText1","81a18cbb8ddd90dd92e882b382ea82c482a282e983818362835a815b835783588373815b836882cc835483938376838b82c582b78142","■现在设定されているメッセ—ジスピ—ドのサンプルです。"},
        {"sampleText2","814093c782dd82e282b782a283588373815b836882c992b290ae82b582c482ad82be82b382a28142","　读みやすいスピ—ドに调整してください。"},
        {"windowMessage1","81a1935f820c8db68fe38a709349825298a2880f88c489c288c88d5889fc94778c6982158eae","■点击左上角的３个图案可以更改背景样式"},
        {"nameWindowMessage","823782329e79","对话框"},
        {"sampleText1","81a18ddd811797a289c2851c832795b68e9a833e8ea691ac93788143","■在这里可测试文字显示速度，"},
        {"sampleText2","81408212850490ae939e95fb95d68ea98cc88f128b08934991ac93788142","　请调整到方便自己阅读的速度。"},
        {"windowMessage1","81a1ea7981148db68fe38a709349338cc29aa488c489c288c88d5889fc94778c699ee98eae","■點擊左上角的3個圖案可以更改背景樣式"},
        {"nameWindowMessage","9b9498629e79","對話框"},
        {"sampleText1","81a18ddd948797a189c291aa8e8e95b68e9ae8fb8ea691ac93788143","■在這裡可測試文字顯示速度，"},
        {"sampleText2","814090bf92b290ae939e95fb95d68ea98cc88524e6a4934991ac93788142","　請調整到方便自己閱讀的速度。"}
    };
    public static void main(String[] args)throws Exception{
        check(GameUiAssets.systemImageKey("TA_Load_Slot01.DWQ").equals("sys/ta_load_slot01")&&
            GameUiAssets.imageKey("TA\\TA_Load_Slot01.DWQ").equals("ta/ta_load_slot01"),"system and script sprites retain distinct routes");
        check(GameUiAssets.systemImageKey("\u0081\\A.DWQ").equals("sys/\u0081\\a"),"CP932 trail byte is not a path separator");
        byte[] japanese=GameResourcePaths.encodeFilename("ソ.DWQ".getBytes(StandardCharsets.UTF_8));
        check(Arrays.equals(japanese,hex("835c2e445751"))&&GameResourcePaths.decodeFilename(japanese).equals("ソ.DWQ"),
            "physical UTF-8 name round-trips the CP932 backslash trail");
        check(GameResourcePaths.encodeFilename("😀".getBytes(StandardCharsets.UTF_8))==null,
            "unrepresentable filenames do not produce question-mark aliases");
        File root=Files.createTempDirectory("systemnnn-config-").toFile();
        try{
            write(new File(root,"SpT/SyS.SpT"),"test",false);
            File script=GameResourcePaths.file(root,"spt","sys.spt");
            check(script!=null&&script.isFile(),"mixed-case script directory and filename");
            check(GameUiAssets.readConfig(root,"title").value("backScriptName","fallback").equals("fallback"),"missing title fallback");
            write(new File(root,"init2/title.fxf"),"backScriptName encrypted_title\r\nfontSize 24\r\nFONTSIZE 42\r\n@fontSize 99\r\nmenu1Button1PrintX 128\r\n",true);
            GameUiAssets.Config config=GameUiAssets.readConfig(root,"title");
            check(config.value("BACKSCRIPTNAME","").equals("encrypted_title"),"FXF XOR and case folding");
            check(config.number("fontSize",0)==24&&config.number("menu1Button1PrintX",0)==128,"FXF numbers and disabled lines");
            write(new File(root,"init/title.xtx"),"backScriptName plain_title\n",false);
            check(GameUiAssets.readConfig(root,"title").value("backScriptName","").equals("encrypted_title"),"PC init2 priority");
            write(new File(root,"init/chart.xtx"),"backScriptFlag 1\nbackScriptName old_plain\n",false);
            write(new File(root,"init2/chart.fxf"),"backScriptFlag 1\nbackScriptName old_chart\npageNumberVarName old_page\nchangePageRestartScriptFlag 1\n",true);
            write(new File(root,"init/selectChart.xtx"),"backScriptFlag 1\nbackScriptName modern_chart\npageNumberVarName modern_page\n",false);
            GameTextDecoder chartDecoder=GameTextDecoder.load(root);
            UiPreviewPool.Request oldChart=UiPreviewPool.Request.read(GameUiAssets.readChartConfig(root,chartDecoder,true),GameRunner.Modal.SELECT_CHART,2);
            UiPreviewPool.Request modernChart=UiPreviewPool.Request.read(GameUiAssets.readChartConfig(root,chartDecoder,false),GameRunner.Modal.SELECT_CHART,2);
            check(oldChart!=null&&oldChart.script.equals("old_chart")&&oldChart.variable.equals("old_page")&&oldChart.restartOnPage,
                "detected legacy chart routes its encrypted original setup into the background request");
            check(modernChart!=null&&modernChart.script.equals("modern_chart")&&modernChart.variable.equals("modern_page")&&
                !modernChart.restartOnPage&&oldChart.mode==34&&modernChart.mode==34&&oldChart.page==2&&!oldChart.sameScript(modernChart),
                "modern chart stays separate while preview mode remains internal and page stays one-based");
            write(new File(root,"init2/messageWindow.fxf"),"filenameWindow1 game_window\nnamePrintX 46\n",true);
            check(GameUiAssets.readConfig(root,"messageWindow").number("namePrintX",0)==46,"message FXF");
            write(new File(root,"init/extended.xtx"),"modePageMax 2\nexpRadioNumber 17\nexpCheckNumber 9\n"+
                "exp1Radio 3\nexp1RadioPrintPage 2\nexp1Radio1Button1PrintX 80\nexp1Radio1Button1SizeX 50\n"+
                "exp2RadioPrintPage 3\n",false);
            OriginalSettingsModel extended=new OriginalSettingsModel(GameUiAssets.readConfig(root,"extended"));
            OriginalSettingsModel.Extended radio=extended.extended.get(0);
            check(extended.original&&radio.page==2&&radio.buttons.length==3&&radio.buttons[2].x==180,
                "extended radio uses original page and previous option geometry");
            check(extended.extended.size()==24&&radio.parameter()==300&&radio.locked(1),"radio count, page bounds and mask");
            check(extended.extended.stream().filter(group->group.index==8).noneMatch(group->group.locked(-1))&&
                extended.extended.stream().anyMatch(group->group.parameter()==400&&group.locked(256)&&!group.locked(1)),
                "only the first eight groups of each kind are maskable");
            write(new File(root,"init/buttonInheritance.xtx"),"modePageMax 3\nmusicVolumePrintX 500\nmusicBarFileName bar\n"+
                "voiceCutButtonNumber 3\nvoiceAutoLineSetFlag 1\nvoiceAutoLinePrintX 10\nvoiceAutoLinePrintY 20\nvoiceAutoLineNextX 150\n"+
                "voice1Button1SizeX 80\nvoice1Button1SizeY 36\nvoice1Button1FileName first\nvoice1Button1CheckSizeX 60\n"+
                "voice1Button1TateYoko 1\nvoice1Button1PicSrcX2 84\nvoice1Button1PicSrcY2 12\nvoice1Button1AnimePattern2 4\n"+
                "voice1Button2SizeX 96\nvoice1Button2SizeY 44\nvoice1Button2FileName active\nvoice1Button2PicSrcY3 78\n"+
                "voice1Button2CheckDeltaX 4\nvoice1Button2CheckPicY 3\nvoice1Button2ClickTime 5\nvoice1Button2ClickSound 7\n"+
                "voice1Button2ClickVoice 6\nvoice1Button2PrintMode 0\nvoice3Button1PrintX 320\nvoice3Button2PrintY 42\n"+
                "tab1Button1SizeX 100\ntab1Button1SizeY 50\ntab1Button1PicSrcX2 15\ntab1Button1AnimePattern2 3\n"+
                "tab1Button2SizeX 120\ntab1Button2PrintX 50\ntab1Button2PrintY 37\n"+
                "systemVoice1Button1FileName theme\nsystemVoice1Button1SizeX 160\n",false);
            write(new File(root,"init/buttonGame.xtx"),"notSrcCopyFlagEnable 1\nuseSystemVoice 2\n",false);
            OriginalSettingsModel buttons=new OriginalSettingsModel(GameUiAssets.readConfig(root,"buttonInheritance"),
                GameUiAssets.readConfig(root,"buttonGame"),1280,720);
            OriginalSettingsModel.Button firstVoice=buttons.characters.get(0).button,thirdVoice=buttons.characters.get(2).button;
            check(firstVoice.off.width==80&&firstVoice.off.height==36&&firstVoice.off.hitWidth==60&&
                firstVoice.on.width==96&&firstVoice.on.height==44&&firstVoice.on.hitWidth==96,
                "each state has its own source and hit dimensions");
            ButtonArt.Setup voice=thirdVoice.off;
            check(voice.width==96&&voice.height==44&&voice.art.image.equals("active")&&voice.art.horizontal&&
                voice.art.states[1].x==84&&voice.art.states[1].y==12&&voice.art.states[1].frames==4&&voice.art.states[2].y==78,
                "third character inherits resolved second-state atlas and animation across omitted fields");
            check(voice.hitX==4&&voice.hitPicY==3&&!voice.pixelHit&&voice.hitWidth==96&&voice.hitHeight==44&&
                voice.clickTime==5&&voice.sound==6&&voice.voice==5,
                "third character inherits click and hit rules, with hit size defaulting to its final dimensions");
            check(thirdVoice.x==320&&thirdVoice.y==20&&thirdVoice.onX==320&&thirdVoice.onY==42,
                "explicit positions override the character grid and the second state inherits the first position");
            OriginalSettingsModel.Button tab=buttons.tabs.get(2);
            check(tab.x==290&&tab.y==37&&tab.off.width==120&&tab.off.art.states[1].x==120&&
                tab.off.art.states[1].frames==3,"radio position inherits previous ON and optional source reset retains animation");
            check(buttons.systemVoices.size()==3&&buttons.systemVoices.get(2).off.art.image.equals("theme")&&
                buttons.systemVoices.get(2).off.width==160,"game system voice count allows sparse atlas declarations");
            String voiceChanges="voiceCutButtonNumber 3\nvoiceNameChangeFlag 1\nvoiceNameChangeVar1 nameFlag\n"+
                "voiceVoiceChangeVar1 soundFlag\nvoiceNameChangeVar2 \u0083\\\nvoiceVoiceChangeVar3 unused\n"+
                "voice1Button1ClickSound 7\nvoice1Button1BadSound 8\nvoice1Button1EnterSound 9\nvoice1Button1ExitSound 10\n"+
                "voice1Button2ClickSound 11\nvoice1Button2ClickVoice 12\nvoice1Button2EnterSound 13\n"+
                "voiceVoiceChangeVoiceOffClick1 41\nvoiceVoiceChangeVoiceOnClick1 42\nvoiceVoiceChangeVoiceOffCannot1 43\n"+
                "voiceVoiceChangeVoiceOffEnter1 44\nvoiceVoiceChangeVoiceOffExit1 45\nvoiceVoiceChangeVoiceOnEnter1 -1\n"+
                "voiceVoiceChangeVoiceOnClick2 46\n";
            File conditional=new File(root,"init/conditional.xtx");
            write(conditional,"voiceVoiceChangeFlag 1\n"+voiceChanges,false);
            OriginalSettingsModel changed=new OriginalSettingsModel(GameUiAssets.readConfig(root,"conditional"));
            OriginalSettingsModel.CharacterControl first=changed.characters.get(0),second=changed.characters.get(1);
            check(first.soundChange.variable.equals("soundFlag")&&second.soundChange.variable.equals("\u0083\\")&&
                changed.characters.get(2).soundChange==null,"explicit and inherited variable names retain their original bytes");
            ButtonArt.SoundEvent click=ButtonArt.SoundEvent.CLICK,enter=ButtonArt.SoundEvent.ENTER;
            check(first.sound(false,click,false)==6&&first.sound(true,click,false)==10&&
                first.sound(false,click,true)==40&&first.sound(true,click,true)==41,"the variable switches the current OFF and ON cues independently");
            check(first.sound(false,ButtonArt.SoundEvent.CANNOT_CLICK,true)==42&&first.sound(false,enter,true)==43&&
                first.sound(false,ButtonArt.SoundEvent.EXIT,true)==44,"the alternate cannot, enter and exit cues use the source fields");
            check(first.sound(true,enter,true)==12&&first.sound(true,ButtonArt.SoundEvent.EXIT,true)==9&&
                first.button.on.voice(click)==11,"nonpositive and absent replacements preserve the inherited cues and button voice");
            write(conditional,"voiceVoiceChangeFlag 0\n"+voiceChanges,false);
            OriginalSettingsModel.CharacterControl unchanged=new OriginalSettingsModel(GameUiAssets.readConfig(root,"conditional")).characters.get(0);
            check(unchanged.soundChange==null&&unchanged.sound(true,click,true)==10,"games must opt into variable-based cues");
            write(new File(root,"init/voiceArt.xtx"),
                "voiceCutButtonNumber 2\nvoiceCutButtonCustomFlag 1\nvoiceCutButtonCheckAppearFlag 1\n"+
                "voiceNameChangeFlag 1\nvoiceNameChangeVar1 portraitMode\n"+
                "voiceOffNumberByButton1 2\nvoiceOffButton1chara1 8\nvoiceOffButton1chara2 9\n"+
                "voice1Button1FileName PortraitOff.DWQ\nvoice1Button2FileName PortraitOn\n",false);
            OriginalSettingsModel voiceArt=new OriginalSettingsModel(GameUiAssets.readConfig(root,"voiceArt"));
            OriginalSettingsModel.CharacterControl portrait=voiceArt.characters.get(0);
            check(!portrait.visible(id->id==1)&&portrait.visible(id->id==8)&&portrait.visible(id->id==9),
                "character appearance uses any heard role in the configured group");
            check(voiceArt.characters.get(1).visible(id->id==10)&&!voiceArt.characters.get(1).visible(id->id==9),
                "character appearance uses inherited role IDs");
            check(portrait.imageVariable.equals("portraitMode")&&portrait.soundChange==null&&
                portrait.variantImage(false,2).equals("sys/portraitoff_2")&&
                portrait.variantImage(true,2).equals("sys/portraiton_2")&&portrait.variantImage(true,0).isEmpty(),
                "character art switches each state independently of the sound setting");
            check(first.visible(id->false)&&first.imageVariable.equals("nameFlag"),
                "the art variable stays independent of the explicitly overridden sound variable");
            write(new File(root,"init2/config.fxf"),"musicVolumePrintX 100\nmusicBarFileName bar\nvolumeNumber 4\nvolumeSizeX1 320\nvolumeSizeY1 13\nfilenameBG first_bg\nfilenameBG3 third_bg\nmodePageMax 3\npageMode 1\nvoiceCutButtonNumber 2\nvoiceCutButtonCustomFlag 1\nvoiceOffNumberByButton1 2\nvoiceOffButton1chara1 7\nvoiceOffNumberByButton2 1\n",true);
            OriginalSettingsModel inherited=new OriginalSettingsModel(GameUiAssets.readConfig(root,"config"));
            check(inherited.pageMode==1&&inherited.sliders.get(3).width==320&&inherited.sliders.get(3).height==13,"source slider size inheritance and page mode");
            check(inherited.background(2).equals("first_bg")&&inherited.background(3).equals("third_bg"),"source page background inheritance");
            check(java.util.Arrays.equals(inherited.characters.get(0).ids,new int[]{7,8})&&inherited.characters.get(1).ids[0]==9,"source custom character ID defaults advance globally");
            OriginalSettingsModel small=new OriginalSettingsModel(GameUiAssets.readConfig(root,"config"),100,50);
            check(small.sliders.get(0).width==100&&small.sliders.get(0).height==13,"small canvas bounds");
            write(new File(root,"init2/config.fxf"),"musicVolumePrintX 0\nmusicBarFileName bar\nvolumeNumber 1\nvolumeSizeX1 1600\nvolumeSizeY1 900\n",true);
            OriginalSettingsModel large=new OriginalSettingsModel(GameUiAssets.readConfig(root,"config"),1920,1080);
            check(large.sliders.get(0).width==1600&&large.sliders.get(0).height==900,"large canvas bounds");
            write(new File(root,"init2/config.fxf"),"volumeNumber 8\nmodePageMax 2\nwindowPercentVolumePrintPage 2\nautoSpeedVolumePrintPage 2\n",true);
            GameUiAssets.Config declared=GameUiAssets.readConfig(root,"config");
            OriginalSettingsModel legacy=new OriginalSettingsModel(declared,null,800,600,true);
            check(new OriginalSettingsModel(declared).sliders.size()==6&&legacy.sliders.size()==8,
                "only legacy profile enables all declared sliders by default");
            for(int i:new int[]{0,6,7})check(legacy.sliders.get(i).button==null,"text and window sliders have no independent switch");
            check(legacy.sliders.get(6).page==2&&legacy.sliders.get(7).page==2,"legacy text-page positions use the config");
            write(new File(root,"init2/config.fxf"),"volumeNumber 8\nautoSpeedVolumeExistFlag 0\nwindowPercentVolumeExistFlag 1\n",true);
            GameUiAssets.Config flags=GameUiAssets.readConfig(root,"config");
            for(boolean old:new boolean[]{true,false}){
                OriginalSettingsModel explicit=new OriginalSettingsModel(flags,null,800,600,old);
                check(explicit.sliders.size()==7&&explicit.sliders.get(6).index==6,"explicit slider flags override the profile");
            }
            write(new File(root,"init/pageDefault.xtx"),"modePageMax 3\nmusicVolumePrintX 100\nmusicBarFileName bar\n",false);
            GameUiAssets.Config pageDefault=GameUiAssets.readConfig(root,"pageDefault");
            OriginalSettingsModel pageModel=new OriginalSettingsModel(pageDefault);
            OriginalSettingsModel fallbackModel=new OriginalSettingsModel(GameUiAssets.readConfig(root,"missingSettings"));
            check(GameSettingsPanel.pageCount(pageModel)==3&&GameSettingsPanel.pageCount(fallbackModel)==2,
                "settings page count covers original and two-page fallback models");
            check(GameSettingsPanel.openingPage(pageDefault,3,0)==0&&
                GameSettingsPanel.openingPage(pageDefault,3,2)==2&&
                GameSettingsPanel.openingPage(pageDefault,3,9)==2&&
                GameSettingsPanel.openingPage(pageDefault,3,-4)==0,
                "omitted MustStartPage1Flag defaults to 0 and clamps remembered page");
            write(new File(root,"init/pageRetain.xtx"),"modePageMax 3\nMustStartPage1Flag 0\n",false);
            check(GameSettingsPanel.openingPage(GameUiAssets.readConfig(root,"pageRetain"),3,1)==1&&
                GameSettingsPanel.openingPage(GameUiAssets.readConfig(root,"pageRetain"),1,1)==0,
                "explicit MustStartPage1Flag 0 retains and clamps remembered page");
            write(new File(root,"init/pageReset.xtx"),"modePageMax 3\nMustStartPage1Flag 1\n",false);
            check(GameSettingsPanel.openingPage(GameUiAssets.readConfig(root,"pageReset"),3,2)==0&&
                GameSettingsPanel.openingPage(GameUiAssets.readConfig(root,"pageReset"),2,1)==0,
                "MustStartPage1Flag 1 resets opening page to page 1 (index 0)");
            for(int i=0;i<9;i++){
                check(GameSystemMenuPanel.standardButtonEnabled(i,false),"normal system menu enables standard slot "+i);
                check(GameSystemMenuPanel.standardButtonEnabled(i,true)==(i>=2),
                    "scene replay disables only Save (0) and Load (1) among standard slots: "+i);
            }
            write(new File(root,"init/systemScene.xtx"),
                "Menu1Button1FileName ta_menu_bt1\nMenu1Button8FileName ta_menu_cancel\nMenu1Button9FileName ta_menu_exit\n"+
                "systemMenuSceneBackFileName ta_scene_back\nsystemMenuSceneEndFileName ta_scene_end\n",false);
            GameUiAssets.Config systemScene=GameUiAssets.readConfig(root,"systemScene");
            check(GameSystemMenuPanel.standardButtonSprite(systemScene,"Menu1Button8","Menu1Button1",7,false,s->true).equals("ta_menu_cancel")&&
                GameSystemMenuPanel.standardButtonSprite(systemScene,"Menu1Button9","Menu1Button1",8,false,s->true).equals("ta_menu_exit"),
                "normal system menu keeps standard cancel and exit button art");
            check(GameSystemMenuPanel.standardButtonSprite(systemScene,"Menu1Button8","Menu1Button1",7,true,s->true).equals("ta_scene_back")&&
                GameSystemMenuPanel.standardButtonSprite(systemScene,"Menu1Button9","Menu1Button1",8,true,s->true).equals("ta_scene_end")&&
                GameSystemMenuPanel.standardButtonSprite(systemScene,"Menu1Button3","Menu1Button1",2,true,s->true).equals("ta_menu_bt1"),
                "scene replay swaps cancel (7) and exit (8) sprites when scene overrides exist");
            check(GameSystemMenuPanel.standardButtonSprite(systemScene,"Menu1Button8","Menu1Button1",7,true,"ta_scene_end"::equals).equals("ta_menu_cancel")&&
                GameSystemMenuPanel.standardButtonSprite(systemScene,"Menu1Button9","Menu1Button1",8,true,"ta_scene_back"::equals).equals("ta_menu_exit"),
                "scene replay falls back to standard button art when override sprite is missing");
            write(new File(root,"init/systemNoScene.xtx"),"Menu1Button8FileName ta_menu_cancel\nMenu1Button9FileName ta_menu_exit\n",false);
            GameUiAssets.Config systemNoScene=GameUiAssets.readConfig(root,"systemNoScene");
            check(GameSystemMenuPanel.standardButtonSprite(systemNoScene,"Menu1Button8","Menu1Button1",7,true,s->{throw new AssertionError("empty override queried");}).equals("ta_menu_cancel")&&
                GameSystemMenuPanel.standardButtonSprite(systemNoScene,"Menu1Button9","Menu1Button1",8,true,s->{throw new AssertionError("empty override queried");}).equals("ta_menu_exit"),
                "empty scene replay sprite names do not query assets and preserve standard art");
            byte[] mapping=new byte[SjisTunnelDecoder.MAX_MAPPINGS*2];
            for(int i=0;i<SjisTunnelDecoder.MAX_MAPPINGS;i++){int cp=0x4e00+i;mapping[i*2]=(byte)cp;mapping[i*2+1]=(byte)(cp>>8);}
            Files.write(new File(root,"sjis_ext.bin").toPath(),mapping);
            Files.write(new File(root,"uif_config.json").toPath(),"{\"character_substitution\":{\"enable\":true,\"source_characters\":\"一\",\"target_characters\":\"壹\"}}".getBytes(StandardCharsets.UTF_8));
            StringBuilder controls=new StringBuilder();
            for(int low=1;low<32;low++)if(low!=9&&low!=10&&low!=13)controls.append("  tunnel").append(low).append("\t\u0081").append((char)low).append(" \t\r\n");
            controls.append("literal\t\u001fA\u0001\t \r\nnotASplit\u000bwrong\tignored\r\nnumber\t24\tcomment\r\nlast \u0081\u0002");
            write(new File(root,"init/controls.xtx"),controls.toString(),false);
            GameUiAssets.Config preserved=GameUiAssets.readConfig(root,"controls");GameTextDecoder expectedCodec=GameTextDecoder.load(root);
            for(int low=1;low<32;low++)if(low!=9&&low!=10&&low!=13){
                byte[] pair={(byte)0x81,(byte)low};String field="tunnel"+low;
                check(Arrays.equals(preserved.value(field,"").getBytes(StandardCharsets.ISO_8859_1),pair),"trailing tunnel byte preserved "+low);
                check(preserved.text(field,"").equals(expectedCodec.decodeConfiguration(pair)),"tunnel/UIF text decoding "+low);
            }
            check(preserved.text("tunnel1","").equals("壹"),"configuration text uses UIF after sjis_ext");
            check(preserved.value("literal","").equals("\u001fA\u0001"),"literal low bytes preserved at both ends");
            check(!preserved.has("notASplit"),"vertical tab is not a key/value delimiter");
            check(preserved.number("number",0)==24,"numeric ASCII tab delimiter");
            check(preserved.value("last","").equals("\u0081\u0002"),"tunnel byte at EOF without newline");
            Files.write(new File(root,"init2/printmessage.fxf").toPath(),new byte[1024*1024+1]);
            try{GameUiAssets.readConfig(root,"printmessage");throw new AssertionError("Oversize config accepted");}catch(IOException expected){checks++;}
            verifyGameDateLayout(root);
            verifySharedConfigSnapshot(new File(root,"shared-cfg"));
            verifyWave25ConfigAlignment(new File(root,"wave25-cfg"));
            verifySettingsRadio();
        }finally{remove(root);}
        if(args.length>0){
            root=new File(args[0]);GameUiAssets.Config title=GameUiAssets.readConfig(root,"title");
            check(title.value("backScriptName","").equals("MNG_backscript"),"real MNG title");
            check(title.number("menu1Button1PrintX",0)==128&&title.number("menu1Button1SizeX",0)==247,"real MNG title geometry");
            check(GameUiAssets.readConfig(root,"printmessage").number("fontSize",0)==24,"real MNG font");
            check(GameUiAssets.readConfig(root,"printmessage").number("printX",0)==230,"real MNG body position");
            check(GameUiAssets.readConfig(root,"messageWindow").number("namePrintX",0)==46,"real MNG name position");
            OriginalSettingsModel model=new OriginalSettingsModel(GameUiAssets.readConfig(root,"config"));
            check(model.original&&model.pages==3&&model.tabs.size()==3,"real three-page settings");
            check(model.sliders.size()==10,"all original volume/speed/opacity sliders");
            for(OriginalSettingsModel.Slider s:model.sliders){
                check(s.width==400&&s.height==14,"indexed original slider dimensions");
                if(s.index==9)check(s.x==654&&s.y==161&&s.key.equals("scriptSoundVolume"),"script SE geometry and separate parameter");
                if(s.index==8)check(s.y==82&&s.key.equals("masterVolume"),"master volume geometry");
                if(s.index==2)check(s.y==201&&s.key.equals("soundVolume"),"system SE geometry");
            }
            check(model.characters.size()==7&&model.characterMin==-10&&model.characterMax==5,"character groups and range");
            check(java.util.Arrays.equals(model.characters.get(6).ids,new int[]{7,8,9,10}),"shared other-character group");
            OriginalSettingsModel.CharacterControl first=model.characters.get(0);
            check(first.x==371&&first.y==448&&first.width==120&&first.height==11,"relative character slider placement");
            check(model.checks.size()==5&&model.modes.size()==3&&model.systemVoices.size()==7,"all original checks/radios/themes");
            check(model.page("systemVoiceButtonPrintPage",1)==3,"theme page declaration");
            check(model.tabs.get(0).on.art.image.equals("ta_cnf_pgbt11"),"radio selected state is Button2");
            check(model.characters.get(0).button.off.art.image.equals("ta_cnf_vocha12"),"character state0 uses Button1 (muted)");
            check(GameUiAssets.readConfig(root,"config").imageNames().contains("sys/ta_cnf_sysvocha71"),"theme images are preloaded from the system route");
            check(GameSettingsPanel.openingPage(GameUiAssets.readConfig(root,"config"),GameSettingsPanel.pageCount(model),2)==0,
                "real MNG MustStartPage1Flag=1 resets settings to first page");
            GameUiAssets.Config mngSystem=GameUiAssets.readConfig(root,"systemmenu");
            String sceneBack=mngSystem.value("systemMenuSceneBackFileName","");
            String sceneEnd=mngSystem.value("systemMenuSceneEndFileName","");
            check(!sceneBack.isEmpty()&&!sceneEnd.isEmpty()&&
                GameSystemMenuPanel.standardButtonSprite(mngSystem,"Menu1Button8","Menu1Button1",7,true,s->true).equals(sceneBack)&&
                GameSystemMenuPanel.standardButtonSprite(mngSystem,"Menu1Button9","Menu1Button1",8,true,s->true).equals(sceneEnd),
                "real MNG systemmenu scene replay back/end sprites");
        }
        for(int i=1;i<args.length;i++){
            File game=new File(args[i]);
            GameUiAssets.Config title=GameUiAssets.readConfig(game,"title");
            check(title.value("backScriptName","").equals("DEA_backscript"),"existing XTX title");
            OriginalSettingsModel model=new OriginalSettingsModel(GameUiAssets.readConfig(game,"config"),
                GameUiAssets.readConfig(game,"game"),1280,720);
            check(model.sliders.stream().filter(slider->slider.button!=null).allMatch(slider->
                slider.button.off.width==76&&slider.button.off.height==40&&slider.button.on.width==76&&slider.button.on.height==40),
                "all DEA audio switches use the declared 76 by 40 cell");
            check(model.characters.size()==6&&model.characters.stream().allMatch(group->
                group.button.off.width==256&&group.button.on.width==256&&group.button.on.height==52&&
                group.button.on.art.states[1].y==52&&group.button.on.art.states[1].frames==4&&group.button.on.art.states[2].y==260),
                "all DEA character states use the inherited atlas geometry and animation");
            check(model.tabs.size()==3&&model.tabs.get(2).on.art.states[1].y==50&&
                model.tabs.get(2).on.art.states[1].frames==4,"DEA third tab inherits hover frames");
            check(model.systemVoices.size()==7&&model.systemVoices.get(0).off.width==192&&
                model.systemVoices.subList(1,7).stream().allMatch(button->button.off.width==272&&button.on.height==52),
                "DEA system voice sizes follow the previous resolved theme instead of the first theme");
        }
        for(String path:args){
            root=new File(path);GameUiAssets.Config config=GameUiAssets.readConfig(root,"config");GameTextDecoder codec=GameTextDecoder.load(root);
            GameUiAssets.Config cursor=GameUiAssets.readConfig(root,"messageCursor");
            int frames=cursor.number("animePattern1",0);
            check(frames>1&&frames==cursor.number("ANIMEPATTERN1",0),"real cursor frame count through case folding: "+root.getName());
            for(String field:new String[]{"windowMessage1","sampleText1","sampleText2"}){
                byte[] original=originalValue(root,field);
                check(Arrays.equals(config.value(field,"").getBytes(StandardCharsets.ISO_8859_1),original),"real complete value bytes: "+root.getName()+"/"+field);
                check(config.text(field,"missing").equals(codec.decodeConfiguration(original)),"real configuration decoder equality: "+root.getName()+"/"+field);
            }
            if(new File(root,"uif_config.json").isFile())check(config.text("sampleText1","").startsWith("■现在设定"),"real MNG FXF + UIF Chinese substitution");
        }
        if(args.length==3){
            for(int game=0;game<3;game++){
                root=new File(args[game]);GameUiAssets.Config config=GameUiAssets.readConfig(root,"config");GameTextDecoder codec=GameTextDecoder.load(root);
                for(int field=0;field<4;field++){
                    String[] fixed=PREVIEWS[game*4+field];byte[] original=originalValue(root,fixed[0]);
                    check(Arrays.equals(original,hex(fixed[1])),"fixed original preview bytes "+game+"/"+fixed[0]);
                    check(codec.decodeConfiguration(hex(fixed[1])).equals(fixed[2]),"fixed complete preview meaning "+game+"/"+fixed[0]);
                    check(config.text(fixed[0],"missing").equals(fixed[2]),"production complete preview meaning "+game+"/"+fixed[0]);
                }
                if(game>0){
                    String[] discriminating=PREVIEWS[game*4+(game==1?1:3)];
                    check(!codec.decode(hex(discriminating[1])).equals(discriminating[2]),"configuration cannot use canonical58 profile "+game);
                }
            }
        }
        System.out.println("PASS "+checks+" configuration checks");
    }

    private static void verifyGameDateLayout(File root) throws Exception {
        // Master gate: template gameWeekPrintFlag=1 alone stays disabled when gameYearPrintFlag=0 & gameDatePrintFlag=0
        write(new File(root,"init/dataDisabled.xtx"),
            "gameWeekPrintFlag 1\nfilenameGameWeekFont ta_mgm_slot_week_act2\ngameWeekPrintX 116\ngameWeekPrintY 15\n",false);
        GameSlotsPanel.GameDateLayout disabled=GameSlotsPanel.GameDateLayout.read(GameUiAssets.readConfig(root,"dataDisabled"));
        check(!disabled.enabled&&!disabled.outside(160,130)&&
            disabled.glyphs(new PcStorySave.GameDate(2026,415)).isEmpty(),
            "gameWeekPrintFlag alone stays gated off when gameYearPrintFlag and gameDatePrintFlag are 0");

        // Full CPrintGameDate(setup, "game") defaults and cascading baseX/baseY when gameYearPrintFlag=1
        write(new File(root,"init/dataCascade.xtx"),"gameYearPrintFlag 1\ngameTimePrintX 20\ngameTimePrintY 30\n",false);
        GameSlotsPanel.GameDateLayout cascade=GameSlotsPanel.GameDateLayout.read(GameUiAssets.readConfig(root,"dataCascade"));
        check(cascade.enabled&&cascade.yearPrint&&cascade.monthPrint&&cascade.dayPrint&&cascade.weekPrint,
            "CPrintGameDate defaults enable month, day, and week when year is enabled");
        check(cascade.timeFont.equals("ta_font")&&cascade.weekFont.equals("ta_font")&&
            cascade.timeSizeX==16&&cascade.timeSizeY==16&&cascade.timeNextX==16,
            "default game date font names and 16x16 cell metrics");
        check(cascade.yearX==20&&cascade.yearY==30&&cascade.monthX==100&&cascade.monthY==30&&
            cascade.dayX==148&&cascade.dayY==30&&cascade.weekX==180&&cascade.weekY==30,
            "cascading baseX/baseY across year (+5*nextX), month (+3*nextX), day (+2*nextX) and week");
        java.util.List<GameSlotsPanel.GameDateLayout.Glyph> full=cascade.glyphs(new PcStorySave.GameDate(2026,405));
        // 4 year digits + year slash (11) + 2 month digits + month slash (11) + 2 day digits + 1 week + you(10) + bi(11) = 13 glyphs
        check(full.size()==13,"full date+week with slashes and you/bi emits 13 glyphs");
        check(full.get(4).cell==11&&full.get(4).x==84&&full.get(7).cell==11&&full.get(7).x==132,
            "year and month slashes use glyph cell 11 at +4*nextX and +2*nextX");
        check(full.get(10).weekSheet&&full.get(10).cell==0&&full.get(10).x==180&&
            full.get(11).weekSheet&&full.get(11).cell==10&&full.get(11).x==196&&
            full.get(12).weekSheet&&full.get(12).cell==11&&full.get(12).x==212,
            "2026-04-05 Sunday (0) week digit and you(10)/bi(11) suffix cells");

        // Ku Little Little / Moerin S profile: upZero=0 right-alignment + unclipped week badge extending past 160px card
        write(new File(root,"init/dataKuLittle.xtx"),
            "filenameGameTimeFont ta_mgm_slot_font_act2\nfilenameGameWeekFont ta_mgm_slot_week_act2\n"+
            "gameDatePrintFlag 1\ngameDateSlashPrintFlag 0\ngameMonthUpZeroPrintFlag 0\ngameDayUpZeroPrintFlag 0\n"+
            "gameTimeFontSizeX 15\ngameTimeFontSizeY 25\ngameTimeFontNextX 13\n"+
            "gameMonthPrintX 10\ngameMonthPrintY 95\ngameDayPrintX 60\ngameDayPrintY 95\n"+
            "gameWeekPrintFlag 1\ngameWeekPrintX 116\ngameWeekPrintY 15\n"+
            "gameWeekFontSizeX 60\ngameWeekFontSizeY 100\ngameWeekYouPrintFlag 0\ngameWeekBiPrintFlag 0\n",false);
        GameSlotsPanel.GameDateLayout ku=GameSlotsPanel.GameDateLayout.read(GameUiAssets.readConfig(root,"dataKuLittle"));
        check(ku.enabled&&!ku.yearPrint&&ku.monthPrint&&ku.dayPrint&&ku.weekPrint&&
            ku.monthSlashFlag==0&&ku.daySlashFlag==0&&ku.weekYouFlag==0&&ku.weekBiFlag==0,
            "Ku Little profile disables year, slashes and week suffixes");
        check(ku.outside(160,130)&&!ku.outside(190,120),
            "week badge 116+60=176 overflows 160px card but fits 190x120 Moerin card");
        java.util.List<GameSlotsPanel.GameDateLayout.Glyph> singleDigits=ku.glyphs(new PcStorySave.GameDate(2000,405));
        check(singleDigits.size()==3&&
            !singleDigits.get(0).weekSheet&&singleDigits.get(0).cell==4&&singleDigits.get(0).x==23&&singleDigits.get(0).y==95&&
            !singleDigits.get(1).weekSheet&&singleDigits.get(1).cell==5&&singleDigits.get(1).x==73&&singleDigits.get(1).y==95&&
            singleDigits.get(2).weekSheet&&singleDigits.get(2).cell==3&&singleDigits.get(2).x==116&&singleDigits.get(2).width==60,
            "CSuuji upZero=0 right-aligns single-digit month/day to +nextX and computes 2000-04-05 Wednesday (3)");
        java.util.List<GameSlotsPanel.GameDateLayout.Glyph> twoDigits=ku.glyphs(new PcStorySave.GameDate(2000,1225));
        check(twoDigits.size()==5&&twoDigits.get(0).cell==2&&twoDigits.get(0).x==23&&
            twoDigits.get(1).cell==1&&twoDigits.get(1).x==10&&
            twoDigits.get(2).cell==5&&twoDigits.get(2).x==73&&
            twoDigits.get(3).cell==2&&twoDigits.get(3).x==60&&
            twoDigits.get(4).weekSheet&&twoDigits.get(4).cell==1,
            "two-digit month/day fills tens at printX and ones at printX+nextX, 2000-12-25 Monday (1)");
        check(ku.timeFont.equals("ta_mgm_slot_font_act2")&&ku.weekFont.equals("ta_mgm_slot_week_act2"),
            "explicit fileNameGameTimeFont and fileNameGameWeekFont preserved without guessing other sheets");

        // MinDeaDBlooD profile: gameDatePrintFlag=1, gameWeekPrintFlag=0, default ta_font when unset
        write(new File(root,"init/dataMdb.xtx"),
            "gameDatePrintFlag 1\ngameDateSlashPrintFlag 0\ngameWeekPrintFlag 0\n"+
            "gameMonthPrintX 10\ngameMonthPrintY 95\ngameDayPrintX 60\ngameDayPrintY 95\n",false);
        GameSlotsPanel.GameDateLayout mdb=GameSlotsPanel.GameDateLayout.read(GameUiAssets.readConfig(root,"dataMdb"));
        check(mdb.enabled&&!mdb.weekPrint&&mdb.timeFont.equals("ta_font")&&mdb.weekFont.isEmpty()&&
            mdb.glyphs(new PcStorySave.GameDate(2000,101)).stream().noneMatch(g->g.weekSheet),
            "MinDeaDBlooD gameWeekPrintFlag=0 suppresses week sprite glyphs and defaults timeFont to ta_font");
    }

    private static void verifySharedConfigSnapshot(File root) throws Exception {
        int[] raw={
            1024,768,16,1,1,3,32,32,24,0,
            3,14,22,120,540,2,28,80,60,-4,32,0,
            35,65,12,3,10,1
        };
        GameUiAssets.SharedConfig snap=GameUiAssets.SharedConfig.from(raw,null,null,null);
        check(snap.fromNative&&snap.screenWidth==1024&&snap.screenHeight==768&&snap.defaultFrameMs==16&&
            snap.codeByte==1&&snap.singleByteText()&&snap.trial()&&snap.systemSaveVersion==3&&
            snap.layerLimit==32&&snap.renderLayerCount==32&&snap.saveLayerRecords==24&&snap.registersOkikae==0&&
            snap.windowMessageNumber==3&&snap.fullMessageNumber==14&&snap.messageFontSize==22&&
            snap.printX==120&&snap.printY==540&&snap.nextX==2&&snap.nextY==28&&
            snap.lprintX==80&&snap.lprintY==60&&snap.lnextX==-4&&snap.lnextY==32&&snap.appendSkipName==0&&
            snap.windowPercentRaw==35&&snap.windowOpacityPercent==65&&snap.charaVoiceCount==12&&
            snap.titleBgmNumber==3&&snap.yearVarIndex==10&&snap.dateVarIndex==1,
            "28-int sharedConfigSnapshot contract maps every field in index order");
        check(snap.gameDateVars().enabled()&&snap.gameDateVars().yearIndex==10&&snap.gameDateVars().dateIndex==1,
            "SharedConfig exposes year/date variable indices for SaveSlots");
        check(GameUiAssets.SharedConfig.opacityFromWindowPercent(0)==100&&
            GameUiAssets.SharedConfig.opacityFromWindowPercent(35)==65&&
            GameUiAssets.SharedConfig.opacityFromWindowPercent(100)==0&&
            GameUiAssets.SharedConfig.opacityFromWindowPercent(-10)==100&&
            GameUiAssets.SharedConfig.opacityFromWindowPercent(150)==0,
            "NNNPARAM_WINDOWPERCENT (106) raw transparency maps to clamped opacity percent");

        DialogueLayout.Text[] baseLayouts=DialogueLayout.layouts(java.util.Collections.emptyMap(),snap);
        DialogueLayout.Text[] longLayouts=DialogueLayout.longLayouts(java.util.Collections.emptyMap(),baseLayouts,snap);
        check(baseLayouts[0].font==22&&baseLayouts[0].x==120&&baseLayouts[0].y==540&&
            baseLayouts[0].nextX==2&&baseLayouts[0].nextY==28&&
            longLayouts[0].x==80&&longLayouts[0].y==60&&longLayouts[0].nextX==-4&&longLayouts[0].nextY==32,
            "DialogueLayout base and LPRINT geometry consume SharedConfig snapshot");

        GameUiAssets.SharedConfig original=GameUiAssets.SharedConfig.fallback(root,null,null);
        check(original.screenWidth==800&&original.screenHeight==600,"PC canvas default without exesetup");
        write(new File(root,"init/mainControl.xtx"),"windowSizeX 960\nwindowSizeY 540\nrealWindowSizeX1 1920\nrealWindowSizeY1 1080\n",false);
        original=GameUiAssets.SharedConfig.fallback(root,null,null);
        check(original.screenWidth==960&&original.screenHeight==540,"window configuration selects the logical buffer, not the physical window");
        write(new File(root,"init2/mainControl.fxf"),"windowSizeX 1024\nwindowSizeY 768\n",true);
        original=GameUiAssets.SharedConfig.fallback(root,null,null);
        check(original.screenWidth==1024&&original.screenHeight==768,"encrypted legacy canvas takes priority");
        write(new File(root,"spt/exesetup.xtx"),"mainScreenWindowSizeX1 800\nmainScreenWindowSizeY1 600\n",false);
        write(new File(root,"init/game.xtx"),"frameTime 25\ncodeByte 1\ntaikenflag 1\nlayerExpand 1\nokikaeNothing 1\n",false);
        write(new File(root,"init/printmessage.xtx"),"windowMessageNumber 3\nfontSize 20\nLPrintX 40\nLPrintY 120\nLNextY 40\nappendSkipName 0\n",false);
        write(new File(root,"init/title.xtx"),"bgmNumber 4\n",false);
        write(new File(root,"spt/var2.fxf"),"v0,date,v2,year\r\n",true);
        GameUiAssets.SharedConfig fb=GameUiAssets.SharedConfig.from(new int[5],root,null,null);
        // Default fullMessageNumber = ((600 - 120) / 40) - 1 = 11
        check(!fb.fromNative&&fb.screenWidth==800&&fb.screenHeight==600&&fb.defaultFrameMs==25&&
            fb.singleByteText()&&fb.trial()&&fb.layerLimit==32&&fb.registersOkikae==0&&
            fb.windowMessageNumber==3&&fb.fullMessageNumber==11&&fb.messageFontSize==20&&
            fb.lprintX==40&&fb.lprintY==120&&fb.lnextY==40&&fb.appendSkipName==0&&
            fb.windowPercentRaw==0&&fb.windowOpacityPercent==100&&fb.titleBgmNumber==4&&
            fb.yearVarIndex==3&&fb.dateVarIndex==1,
            "SharedConfig pure-Java fallback computes exesetup/game/printmessage/title/var2 defaults when native handle is absent");
    }

    private static void verifySettingsRadio() {
        int[] changes={0};
        Runnable select=()->changes[0]++;
        check(GameSettingsPanel.radioAction(true,select)==null,"selected configuration radio has no click action");
        GameSettingsPanel.radioAction(false,select).run();
        check(changes[0]==1,"unselected configuration radio can apply its choice");
        for(boolean fullscreen:new boolean[]{false,true}){
            SharedPreferences preferences=(SharedPreferences)Proxy.newProxyInstance(
                SharedPreferences.class.getClassLoader(),new Class<?>[]{SharedPreferences.class},
                (p,m,a)->Map.of("fullscreen",fullscreen));
            int[] pairs=RuntimeSettingsAdapter.changedParameter(preferences,null,"fullscreen");
            check(Arrays.equals(pairs,new int[]{4,fullscreen?1:0}),"window mode uses the PC screen parameter");
        }
        File game=new File(".");
        String prefix=RuntimeSettingsAdapter.ownerKey(game,0).replace("owner.0","");
        for(int auto:new int[]{0,1}){
            SharedPreferences preferences=(SharedPreferences)Proxy.newProxyInstance(
                SharedPreferences.class.getClassLoader(),new Class<?>[]{SharedPreferences.class},
                (p,m,a)->Map.of(prefix+6,auto,prefix+105,72));
            check(Arrays.equals(RuntimeSettingsAdapter.persistedOverrides(preferences,game),new int[]{6,auto,105,72}),
                "ordinary startup retains the persisted Auto preference");
            check(Arrays.equals(RuntimeSettingsAdapter.persistedOverrides(preferences,game,false),new int[]{105,72}),
                "PC load must retain its runtime Auto decision independently of persisted settings");
        }
    }

    private static void verifyWave25ConfigAlignment(File root) throws Exception {
        write(new File(root,"init/winPercent.xtx"),
            "volumeNumber 8\nwindowPercentVolumeExistFlag 1\nwindowPercentVolumeDevide 20\n",false);
        OriginalSettingsModel wpModel=new OriginalSettingsModel(GameUiAssets.readConfig(root,"winPercent"));
        OriginalSettingsModel.Slider wpSlider=wpModel.sliders.stream()
            .filter(s->s.index==6).findFirst().orElseThrow(()->new AssertionError("missing windowPercent slider"));
        check(wpSlider.steps==20&&wpSlider.toPreference(0)==0&&wpSlider.toPreference(35)==35&&
            wpSlider.toPreference(100)==100&&wpSlider.fromPreference(65)==65&&wpSlider.fromPreference(140)==100,
            "windowPercent slider preserves raw 0..100 transparency/opacity values without scaling by steps");

        write(new File(root,"init/backlogDefault.xtx"),"",false);
        write(new File(root,"init/backlogMode2.xtx"),"bgMode 2\nbackMultiR 50\nbackDeltaR -10\n",false);
        write(new File(root,"init/backlogScript.xtx"),"bgMode 1\nbackScriptFlag 1\n",false);
        write(new File(root,"init/backlogOutOfRange.xtx"),"bgMode 9\n",false);
        check(GameBacklogPanel.bgMode(GameUiAssets.readConfig(root,"backlogDefault"))==0&&
            GameBacklogPanel.bgMode(GameUiAssets.readConfig(root,"backlogMode2"))==2&&
            GameBacklogPanel.bgMode(GameUiAssets.readConfig(root,"backlogScript"))==3&&
            GameBacklogPanel.bgMode(GameUiAssets.readConfig(root,"backlogOutOfRange"))==0,
            "GameBacklogPanel.bgMode honors 0..3, backScriptFlag override, and 0 fallback");
        check(GameBacklogPanel.colorForTag('C')==0xff00ffff&&
            GameBacklogPanel.colorForTag((char)0x6c34)==0xff00ffff&&
            GameBacklogPanel.colorForTag('O')==0xffff8000&&
            GameBacklogPanel.colorForTag((char)0x6a59)==0xffff8000&&
            GameBacklogPanel.colorForTag('Z')==0,
            "GameBacklogPanel.colorForTag maps upstream ASCII and kanji color prefixes");

        int[] pixels={0xffc86432};
        GameRunner.filter(pixels,GameUiAssets.readConfig(root,"backlogMode2"));
        // mr = 50 * 256 / 100 = 128; (200*128>>8)-10 = 90, (100*128>>8)-10 = 40, (50*128>>8)-10 = 15
        check(pixels[0]==(0xff000000|(90<<16)|(40<<8)|15),
            "GameRunner.filter bgMode==2 cascades backMultiR and backDeltaR across RGB channels");
    }
}
