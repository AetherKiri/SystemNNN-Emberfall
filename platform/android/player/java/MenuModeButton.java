package com.tyranor.runtime.systemnnn;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Extra CMenuButtonSetup entries whose configured action opens an original mode. */
final class MenuModeButton {
    final int index,mode,x,y;
    // Nonzero starts a new story and stores this number in the title's extModeReturnVarName.
    final int scenario;
    final String label;
    final boolean enabled;
    final ButtonArt.Setup setup;

    private MenuModeButton(int index,int mode,int scenario,int x,int y,String label,boolean enabled,ButtonArt.Setup setup){
        this.index=index;this.mode=mode;this.scenario=scenario;this.x=x;this.y=y;
        this.label=label;this.enabled=enabled;this.setup=setup;
    }

    static List<MenuModeButton> read(GameUiAssets.Config config,long handle,int basic,int builtIn){
        return read(config,handle,basic,builtIn,false);
    }

    static List<MenuModeButton> readTitle(GameUiAssets.Config config,long handle,int basic,int builtIn){
        return read(config,handle,basic,builtIn,true);
    }

    private static List<MenuModeButton> read(GameUiAssets.Config config,long handle,int basic,int builtIn,boolean includeBasic){
        int modes=DialogueLayout.clamp(config.number("extModeNumber",0),0,32);
        int extra=modes==0?0:DialogueLayout.clamp(config.number("extButtonNumber",1),0,64);
        String variable=config.value("extModeCheckVarName","");
        int mode=modes==0||variable.isEmpty()?0:NativeGame.menuVariable(handle,variable.getBytes(StandardCharsets.ISO_8859_1));
        if(mode<0||mode>modes)mode=0;
        List<MenuModeButton> buttons=new ArrayList<>();
        ButtonArt.Setup previous=null;
        int x=0,y=0;
        int nextX=config.number("menuNextX",0),nextY=config.number("menuNextY",config.number("menu1Button1SizeY",16));
        for(int index=0;index<basic+extra;index++){
            String tag="menu1Button"+(index+1);
            if(index>0){x+=nextX;y+=nextY;}
            x=config.number(tag+"PrintX",x);y=config.number(tag+"PrintY",y);
            previous=new ButtonArt.Setup(config,tag,previous,false);
            int target=index<basic?config.number("basicButtonSpecialMode"+(index+1),-1):
                config.number("extButtonSpecialMode"+(index-basic+1),-1);
            // CCommonTitle starts a story from a button without a mode. Extension buttons
            // count up from one and added basic buttons count down from minus one.
            int scenario=0;
            if(index<builtIn){
                if(!includeBasic||index>=basic)continue;
            }else if(target>49)continue;
            else if(target<=0){
                if(!includeBasic)continue;
                scenario=index<basic?-(index-builtIn+1):index-builtIn+1;
            }
            boolean exists=config.number(tag+"ExistFlag",index<basic?1:0)!=0;
            boolean enabled=config.number(tag+"EnableFlag",exists?1:0)!=0;
            ButtonArt.Setup resolved=previous;
            int atX=x,atY=y;
            for(int variant=1;variant<=mode;variant++){
                tag="menu"+(variant+1)+"Button"+(index+1);
                if(variant==1&&index>=basic)exists=true;
                exists=config.number(tag+"ExistFlag",exists?1:0)!=0;
                enabled=config.number(tag+"EnableFlag",exists&&(index>=basic||enabled)?1:0)!=0;
                resolved=new ButtonArt.Setup(config,tag,resolved,false);
                atX=config.number(tag+"PrintX",atX);atY=config.number(tag+"PrintY",atY);
            }
            if(exists)buttons.add(new MenuModeButton(index,scenario!=0?0:target,scenario,atX,atY,
                config.text(tag+"Name",scenario!=0?"剧情 · "+Math.abs(scenario):label(target)),enabled,resolved));
        }
        return buttons;
    }

    private static String label(int mode){
        switch(mode){
        case 24:return "图鉴";
        case 29:return "姓名输入";
        case 33:return "结果";
        case 34:return "流程图";
        case 36:case 39:return "提示";
        case 38:return "状态";
        case 42:return "成就";
        case 43:return "术语";
        case 44:return "语音鉴赏";
        case 47:return "道具";
        case 48:return "商店";
        case 49:return "队伍";
        default:return "扩展 · "+mode;
        }
    }
}
